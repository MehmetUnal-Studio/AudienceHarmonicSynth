#!/usr/bin/env node

import dgram from 'node:dgram';
import { createReadStream, createWriteStream } from 'node:fs';
import { pathToFileURL } from 'node:url';

const CAPTURE_VERSION = 1;
const MAX_UDP_PAYLOAD = 65_507;
const MAX_CAPTURE_LINE_BYTES = 192 * 1024;
const DEFAULT_MAX_QUEUE = 8_192;
const DEFAULT_MAX_INFLIGHT = 64;
const REORDER_IDLE_FLUSH_MS = 25;

export class CliError extends Error {
    constructor(message) {
        super(message);
        this.name = 'CliError';
    }
}

function assertCondition(condition, message, ErrorType = Error) {
    if (!condition)
        throw new ErrorType(message);
}

function integerInRange(value, name, minimum, maximum) {
    const number = Number(value);
    if (!Number.isSafeInteger(number) || number < minimum || number > maximum)
        throw new CliError(`${name} must be an integer in ${minimum}..${maximum}`);
    return number;
}

function numberInRange(value, name, minimum, maximum) {
    const number = Number(value);
    if (!Number.isFinite(number) || number < minimum || number > maximum)
        throw new CliError(`${name} must be a finite number in ${minimum}..${maximum}`);
    return number;
}

export function hashSeed(seed) {
    const text = String(seed);
    assertCondition(text.length > 0, 'seed must not be empty', CliError);
    let hash = 0x811c9dc5;
    for (let index = 0; index < text.length; ++index) {
        hash ^= text.charCodeAt(index);
        hash = Math.imul(hash, 0x01000193);
    }
    return hash >>> 0;
}

export class SeededRandom {
    constructor(seed = 'cosmic-chaos-lab') {
        this.state = hashSeed(seed);
    }

    nextUint32() {
        this.state = (this.state + 0x6d2b79f5) >>> 0;
        let value = this.state;
        value = Math.imul(value ^ (value >>> 15), value | 1);
        value ^= value + Math.imul(value ^ (value >>> 7), value | 61);
        return (value ^ (value >>> 14)) >>> 0;
    }

    next() {
        return this.nextUint32() / 0x1_0000_0000;
    }
}

export function parseBurstPolicy(specification = '0') {
    if (specification === undefined || specification === null || String(specification) === '0')
        return { probability: 0, length: 0 };

    const match = String(specification).match(/^([^:]+):(\d+)$/);
    if (!match)
        throw new CliError('burst must be 0 or PROBABILITY:LENGTH (for example 0.02:4)');

    const probability = numberInRange(match[1], 'burst probability', 0, 1);
    const length = integerInRange(match[2], 'burst length', 1, 4_096);
    return probability === 0 ? { probability: 0, length: 0 } : { probability, length };
}

export function normalizeChaosOptions(options = {}) {
    const drop = numberInRange(options.drop ?? 0, 'drop', 0, 1);
    const duplicate = numberInRange(options.duplicate ?? 0, 'duplicate', 0, 1);
    const reorderWindow = integerInRange(options.reorderWindow ?? options.reorder ?? 0,
                                         'reorder', 0, 4_096);
    const jitterMs = numberInRange(options.jitterMs ?? options.jitter ?? 0,
                                   'jitter', 0, 60_000);
    const burst = typeof options.burst === 'object'
        ? options.burst
        : parseBurstPolicy(options.burst ?? '0');
    assertCondition(Number.isFinite(burst.probability)
                    && burst.probability >= 0 && burst.probability <= 1,
                    'burst probability must be in 0..1', CliError);
    assertCondition(Number.isSafeInteger(burst.length)
                    && burst.length >= 0 && burst.length <= 4_096,
                    'burst length must be an integer in 0..4096', CliError);
    assertCondition((burst.probability === 0) === (burst.length === 0),
                    'burst probability and length must both be zero or both be enabled', CliError);

    return {
        seed: options.seed ?? 'cosmic-chaos-lab',
        drop,
        duplicate,
        reorderWindow,
        jitterMs,
        burst: { probability: burst.probability, length: burst.length }
    };
}

export class ChaosPolicy {
    constructor(options = {}) {
        this.options = normalizeChaosOptions(options);
        this.random = new SeededRandom(this.options.seed);
        this.reorderBuffer = [];
        this.burstDropsRemaining = 0;
        this.stats = {
            inputPackets: 0,
            droppedPackets: 0,
            burstDroppedPackets: 0,
            duplicatesCreated: 0,
            outputPackets: 0,
            maxReorderBuffered: 0
        };
    }

    get bufferedCount() {
        return this.reorderBuffer.length;
    }

    accept(packet) {
        ++this.stats.inputPackets;

        if (this.burstDropsRemaining > 0) {
            --this.burstDropsRemaining;
            ++this.stats.burstDroppedPackets;
            ++this.stats.droppedPackets;
            return [];
        }

        const { burst, drop, duplicate } = this.options;
        if (burst.length > 0 && this.random.next() < burst.probability) {
            this.burstDropsRemaining = burst.length - 1;
            ++this.stats.burstDroppedPackets;
            ++this.stats.droppedPackets;
            return [];
        }

        if (drop > 0 && this.random.next() < drop) {
            ++this.stats.droppedPackets;
            return [];
        }

        const candidates = [{ packet, duplicate: false }];
        if (duplicate > 0 && this.random.next() < duplicate) {
            candidates.push({ packet, duplicate: true });
            ++this.stats.duplicatesCreated;
        }

        const released = [];
        for (const candidate of candidates)
            released.push(...this.#acceptForReorder(candidate));
        return this.#withJitter(released);
    }

    flush() {
        if (this.reorderBuffer.length === 0)
            return [];
        const released = this.#shuffle(this.reorderBuffer.splice(0));
        return this.#withJitter(released);
    }

    discard() {
        this.reorderBuffer.length = 0;
        this.burstDropsRemaining = 0;
    }

    #acceptForReorder(candidate) {
        const window = this.options.reorderWindow;
        if (window <= 1)
            return [candidate];

        this.reorderBuffer.push(candidate);
        this.stats.maxReorderBuffered = Math.max(this.stats.maxReorderBuffered,
                                                 this.reorderBuffer.length);
        if (this.reorderBuffer.length < window)
            return [];

        return this.#shuffle(this.reorderBuffer.splice(0));
    }

    #shuffle(items) {
        for (let index = items.length - 1; index > 0; --index) {
            const other = Math.floor(this.random.next() * (index + 1));
            [items[index], items[other]] = [items[other], items[index]];
        }
        return items;
    }

    #withJitter(candidates) {
        const outputs = candidates.map((candidate) => ({
            ...candidate,
            delayMs: this.options.jitterMs > 0
                ? this.random.next() * this.options.jitterMs
                : 0
        }));
        this.stats.outputPackets += outputs.length;
        return outputs;
    }
}

function oscString(value) {
    assertCondition(typeof value === 'string' && !value.includes('\0'),
                    'OSC strings must be NUL-free strings');
    const bytes = Buffer.from(value, 'utf8');
    const paddedLength = (bytes.length + 1 + 3) & ~3;
    const output = Buffer.alloc(paddedLength);
    bytes.copy(output);
    return output;
}

export function encodeOscMessage(address, argumentsList = []) {
    assertCondition(typeof address === 'string' && address.startsWith('/')
                    && !address.includes('\0'),
                    'OSC address must be a NUL-free string beginning with /');
    assertCondition(Array.isArray(argumentsList), 'OSC arguments must be an array');

    const tags = [','];
    const encodedArguments = [];
    for (const argument of argumentsList) {
        assertCondition(argument && typeof argument === 'object',
                        'each OSC argument must be a typed object');
        const { type, value } = argument;
        if (type === 'i') {
            assertCondition(Number.isInteger(value)
                            && value >= -2_147_483_648 && value <= 2_147_483_647,
                            'OSC int32 argument is out of range');
            const bytes = Buffer.alloc(4);
            bytes.writeInt32BE(value, 0);
            encodedArguments.push(bytes);
        } else if (type === 'f') {
            assertCondition(Number.isFinite(value), 'OSC float32 argument must be finite');
            const bytes = Buffer.alloc(4);
            bytes.writeFloatBE(value, 0);
            encodedArguments.push(bytes);
        } else if (type === 's') {
            encodedArguments.push(oscString(value));
        } else {
            throw new Error(`unsupported OSC argument type: ${String(type)}`);
        }
        tags.push(type);
    }

    return Buffer.concat([oscString(address), oscString(tags.join('')), ...encodedArguments]);
}

function readOscString(payload, start) {
    const terminator = payload.indexOf(0, start);
    assertCondition(terminator >= start, 'unterminated OSC string');
    const next = (terminator + 1 + 3) & ~3;
    assertCondition(next <= payload.length, 'truncated OSC string padding');
    for (let index = terminator + 1; index < next; ++index)
        assertCondition(payload[index] === 0, 'non-zero OSC string padding');
    return { value: payload.toString('utf8', start, terminator), next };
}

export function decodeOscMessage(payload) {
    assertCondition(Buffer.isBuffer(payload), 'OSC payload must be a Buffer');
    const address = readOscString(payload, 0);
    assertCondition(address.value.startsWith('/'), 'OSC address must begin with /');
    const typeTags = readOscString(payload, address.next);
    assertCondition(typeTags.value.startsWith(','), 'OSC type tag string must begin with comma');
    let offset = typeTags.next;
    const args = [];
    for (const type of typeTags.value.slice(1)) {
        if (type === 'i') {
            assertCondition(offset + 4 <= payload.length, 'truncated OSC int32');
            args.push({ type, value: payload.readInt32BE(offset) });
            offset += 4;
        } else if (type === 'f') {
            assertCondition(offset + 4 <= payload.length, 'truncated OSC float32');
            args.push({ type, value: payload.readFloatBE(offset) });
            offset += 4;
        } else if (type === 's') {
            const decoded = readOscString(payload, offset);
            args.push({ type, value: decoded.value });
            offset = decoded.next;
        } else {
            throw new Error(`unsupported OSC type tag: ${type}`);
        }
    }
    assertCondition(offset === payload.length, 'trailing bytes after OSC message');
    return { address: address.value, args };
}

export function extractOscAddress(payload) {
    if (!Buffer.isBuffer(payload) || payload.length === 0)
        return '<invalid>';
    try {
        const first = readOscString(payload, 0).value;
        if (first === '#bundle')
            return '#bundle';
        return first.startsWith('/') ? first : '<invalid>';
    } catch {
        return '<invalid>';
    }
}

export function isLifecycleAddress(address) {
    return typeof address === 'string'
        && (address.toLowerCase().endsWith('/on')
            || address.toLowerCase().endsWith('/off'));
}

function canonicalBase64(value) {
    return typeof value === 'string'
        && value.length % 4 === 0
        && /^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/.test(value);
}

export function normalizeCaptureRecord(record) {
    assertCondition(record && typeof record === 'object' && !Array.isArray(record),
                    'capture record must be an object');
    assertCondition(record.v === CAPTURE_VERSION,
                    `capture record version must be ${CAPTURE_VERSION}`);
    assertCondition(Number.isSafeInteger(record.deltaMicros) && record.deltaMicros >= 0,
                    'capture deltaMicros must be a non-negative safe integer');
    assertCondition(typeof record.wallTime === 'string'
                    && Number.isFinite(Date.parse(record.wallTime)),
                    'capture wallTime must be an ISO-compatible timestamp');
    assertCondition(Number.isInteger(record.port) && record.port >= 1 && record.port <= 65_535,
                    'capture port must be in 1..65535');
    assertCondition(typeof record.address === 'string' && record.address.length <= 4_096,
                    'capture address must be a bounded string');
    assertCondition(canonicalBase64(record.payload), 'capture payload must be canonical base64');
    const payloadBytes = Buffer.from(record.payload, 'base64');
    assertCondition(payloadBytes.length <= MAX_UDP_PAYLOAD,
                    `capture payload exceeds ${MAX_UDP_PAYLOAD} UDP bytes`);

    const normalized = {
        v: CAPTURE_VERSION,
        deltaMicros: record.deltaMicros,
        wallTime: record.wallTime,
        port: record.port,
        address: record.address,
        payload: record.payload
    };
    if (record.remoteAddress !== undefined) {
        assertCondition(typeof record.remoteAddress === 'string'
                        && record.remoteAddress.length > 0 && record.remoteAddress.length <= 255,
                        'capture remoteAddress must be a bounded string');
        normalized.remoteAddress = record.remoteAddress;
    }
    if (record.remotePort !== undefined) {
        assertCondition(Number.isInteger(record.remotePort)
                        && record.remotePort >= 0 && record.remotePort <= 65_535,
                        'capture remotePort must be in 0..65535');
        normalized.remotePort = record.remotePort;
    }
    return normalized;
}

export function createCaptureRecord({ startMonoNs, nowMonoNs = process.hrtime.bigint(),
                                      wallTimeMs = Date.now(), port, remote, payload }) {
    assertCondition(typeof startMonoNs === 'bigint' && typeof nowMonoNs === 'bigint'
                    && nowMonoNs >= startMonoNs,
                    'capture monotonic timestamps are invalid');
    assertCondition(Buffer.isBuffer(payload), 'capture payload must be a Buffer');
    const deltaMicrosBig = (nowMonoNs - startMonoNs) / 1_000n;
    assertCondition(deltaMicrosBig <= BigInt(Number.MAX_SAFE_INTEGER),
                    'capture duration exceeds safe NDJSON delta range');
    return normalizeCaptureRecord({
        v: CAPTURE_VERSION,
        deltaMicros: Number(deltaMicrosBig),
        wallTime: new Date(wallTimeMs).toISOString(),
        port,
        address: extractOscAddress(payload),
        payload: payload.toString('base64'),
        remoteAddress: remote?.address,
        remotePort: remote?.port
    });
}

export function serializeCaptureRecord(record) {
    return `${JSON.stringify(normalizeCaptureRecord(record))}\n`;
}

export function parseCaptureLine(line) {
    assertCondition(typeof line === 'string' && Buffer.byteLength(line) <= MAX_CAPTURE_LINE_BYTES,
                    'capture line is not a bounded string');
    let parsed;
    try {
        parsed = JSON.parse(line);
    } catch (error) {
        throw new Error(`invalid capture JSON: ${error.message}`);
    }
    return normalizeCaptureRecord(parsed);
}

export async function* readCaptureRecords(path, { signal } = {}) {
    const input = createReadStream(path, { highWaterMark: 64 * 1024 });
    input.setEncoding('utf8');
    const abort = () => input.destroy(new AbortError());
    if (signal?.aborted)
        abort();
    else
        signal?.addEventListener('abort', abort, { once: true });

    let pending = '';
    let lineNumber = 0;
    try {
        for await (const chunk of input) {
            pending += chunk;
            if (Buffer.byteLength(pending) > MAX_CAPTURE_LINE_BYTES
                && !pending.includes('\n'))
                throw new Error(`capture line ${lineNumber + 1} exceeds the size limit`);

            let newline;
            while ((newline = pending.indexOf('\n')) >= 0) {
                const line = pending.slice(0, newline).replace(/\r$/, '');
                pending = pending.slice(newline + 1);
                ++lineNumber;
                if (line.length === 0)
                    continue;
                try {
                    yield parseCaptureLine(line);
                } catch (error) {
                    throw new Error(`capture line ${lineNumber}: ${error.message}`);
                }
            }
        }
        if (pending.length > 0) {
            ++lineNumber;
            try {
                yield parseCaptureLine(pending.replace(/\r$/, ''));
            } catch (error) {
                throw new Error(`capture line ${lineNumber}: ${error.message}`);
            }
        }
    } finally {
        signal?.removeEventListener('abort', abort);
    }
}

export class BoundedNdjsonWriter {
    constructor(path, { maxQueue = DEFAULT_MAX_QUEUE, overwrite = false } = {}) {
        this.maxQueue = integerInRange(maxQueue, 'max queue', 1, 262_144);
        this.queue = [];
        this.queueHead = 0;
        this.blocked = false;
        this.closing = false;
        this.error = null;
        this.dropped = 0;
        this.written = 0;
        this.stream = createWriteStream(path, {
            flags: overwrite ? 'w' : 'wx',
            highWaterMark: 64 * 1024
        });
        this.readyPromise = new Promise((resolve, reject) => {
            this.stream.once('open', resolve);
            this.stream.once('error', reject);
        });
        this.stream.on('error', (error) => { this.error = error; });
        this.stream.on('drain', () => {
            this.blocked = false;
            this.#flush();
        });
    }

    async ready() {
        await this.readyPromise;
    }

    write(record) {
        if (this.closing)
            return false;
        if (this.error)
            throw this.error;
        const line = serializeCaptureRecord(record);
        if (this.blocked) {
            if (this.queue.length - this.queueHead >= this.maxQueue) {
                ++this.dropped;
                return false;
            }
            this.queue.push(line);
            return true;
        }
        this.#writeLine(line);
        return true;
    }

    #writeLine(line) {
        ++this.written;
        if (!this.stream.write(line))
            this.blocked = true;
    }

    #flush() {
        while (!this.blocked && this.queueHead < this.queue.length)
            this.#writeLine(this.queue[this.queueHead++]);
        if (this.queueHead === this.queue.length) {
            this.queue.length = 0;
            this.queueHead = 0;
        }
    }

    async close() {
        if (this.closing)
            return;
        this.closing = true;
        this.#flush();
        while ((this.blocked || this.queueHead < this.queue.length) && !this.error) {
            await new Promise((resolve) => {
                const wake = () => {
                    this.stream.off('drain', wake);
                    this.stream.off('error', wake);
                    resolve();
                };
                this.stream.once('drain', wake);
                this.stream.once('error', wake);
            });
            this.#flush();
        }
        if (this.error)
            throw this.error;
        await new Promise((resolve, reject) => {
            const onError = (error) => { this.stream.off('finish', resolve); reject(error); };
            this.stream.once('error', onError);
            this.stream.once('finish', resolve);
            this.stream.end();
        });
    }
}

export function parseHostPort(specification) {
    const text = String(specification ?? '');
    const separator = text.lastIndexOf(':');
    if (separator <= 0 || separator === text.length - 1)
        throw new CliError(`target must be HOST:PORT, got ${JSON.stringify(text)}`);
    const host = text.slice(0, separator);
    if (!host || host.includes(':') || /\s|\0/.test(host))
        throw new CliError('target host must be an IPv4 address or DNS hostname (IPv6 is not supported)');
    return { host, port: integerInRange(text.slice(separator + 1), 'target port', 1, 65_535) };
}

export function parseForwardMapping(specification) {
    const text = String(specification ?? '');
    const equals = text.indexOf('=');
    const separator = equals >= 0 ? equals : text.indexOf(':');
    if (separator <= 0)
        throw new CliError(`map must be LISTEN_PORT=HOST:TARGET_PORT, got ${JSON.stringify(text)}`);
    return {
        listenPort: integerInRange(text.slice(0, separator), 'listen port', 1, 65_535),
        ...parseHostPort(text.slice(separator + 1))
    };
}

export function buildMappingTable(specifications) {
    assertCondition(Array.isArray(specifications) && specifications.length > 0,
                    'at least one --map is required', CliError);
    const table = new Map();
    for (const specification of specifications) {
        const mapping = typeof specification === 'string'
            ? parseForwardMapping(specification)
            : specification;
        assertCondition(!table.has(mapping.listenPort),
                        `duplicate mapping for UDP ${mapping.listenPort}`, CliError);
        table.set(mapping.listenPort, mapping);
    }
    return table;
}

export function resolveMapping(table, listenPort) {
    const mapping = table.get(listenPort);
    if (!mapping)
        throw new Error(`capture port ${listenPort} has no --map destination`);
    return mapping;
}

class DatagramPriorityQueue {
    constructor(capacity) {
        this.capacity = capacity;
        this.lifecycle = [];
        this.lifecycleHead = 0;
        this.motion = new Map();
        this.droppedLifecycle = 0;
        this.droppedMotion = 0;
        this.coalescedMotion = 0;
    }

    get size() {
        return this.lifecycle.length - this.lifecycleHead + this.motion.size;
    }

    enqueue(item) {
        if (item.priority) {
            if (this.size >= this.capacity) {
                const oldestMotion = this.motion.keys().next();
                if (!oldestMotion.done) {
                    this.motion.delete(oldestMotion.value);
                    ++this.droppedMotion;
                } else {
                    ++this.droppedLifecycle;
                    return false;
                }
            }
            this.lifecycle.push(item);
            return true;
        }

        const key = item.key;
        if (this.motion.has(key)) {
            this.motion.set(key, item);
            ++this.coalescedMotion;
            return true;
        }
        if (this.size >= this.capacity) {
            ++this.droppedMotion;
            return false;
        }
        this.motion.set(key, item);
        return true;
    }

    shift() {
        if (this.lifecycleHead < this.lifecycle.length) {
            const item = this.lifecycle[this.lifecycleHead++];
            if (this.lifecycleHead > 1_024 && this.lifecycleHead * 2 > this.lifecycle.length) {
                this.lifecycle = this.lifecycle.slice(this.lifecycleHead);
                this.lifecycleHead = 0;
            }
            return item;
        }
        const first = this.motion.entries().next();
        if (first.done)
            return undefined;
        this.motion.delete(first.value[0]);
        return first.value[1];
    }

    clearMotion() {
        this.motion.clear();
    }

    clear() {
        this.lifecycle.length = 0;
        this.lifecycleHead = 0;
        this.motion.clear();
    }
}

export class BoundedDatagramSender {
    constructor({ maxQueue = DEFAULT_MAX_QUEUE, maxInflight = DEFAULT_MAX_INFLIGHT } = {}) {
        this.queue = new DatagramPriorityQueue(integerInRange(maxQueue, 'max queue', 1, 262_144));
        this.maxInflight = integerInRange(maxInflight, 'max inflight', 1, 4_096);
        this.socket = dgram.createSocket('udp4');
        this.inflight = 0;
        this.accepting = true;
        this.used = false;
        this.closed = false;
        this.sent = 0;
        this.sendErrors = 0;
        this.waiters = new Set();
        this.socket.on('error', () => { ++this.sendErrors; });
    }

    enqueue(item) {
        if (!this.accepting)
            return false;
        const address = item.address ?? extractOscAddress(item.payload);
        const queued = this.queue.enqueue({
            ...item,
            address,
            priority: item.priority ?? isLifecycleAddress(address),
            key: item.key ?? `${item.host}:${item.port}:${address}`
        });
        this.#pump();
        return queued;
    }

    #pump() {
        while (this.inflight < this.maxInflight) {
            const item = this.queue.shift();
            if (!item)
                break;
            ++this.inflight;
            this.used = true;
            try {
                this.socket.send(item.payload, item.port, item.host, (error) => {
                    --this.inflight;
                    if (error)
                        ++this.sendErrors;
                    else
                        ++this.sent;
                    this.#pump();
                    this.#notifyIfDrained();
                });
            } catch {
                --this.inflight;
                ++this.sendErrors;
            }
        }
        this.#notifyIfDrained();
    }

    #notifyIfDrained() {
        if (this.inflight !== 0 || this.queue.size !== 0)
            return;
        for (const resolve of this.waiters)
            resolve(true);
        this.waiters.clear();
    }

    clearMotion() {
        this.queue.clearMotion();
    }

    clearQueued() {
        this.queue.clear();
    }

    async waitUntilDrained(signal) {
        if (this.inflight === 0 && this.queue.size === 0)
            return true;
        return new Promise((resolve) => {
            const done = (value) => {
                signal?.removeEventListener('abort', onAbort);
                this.waiters.delete(done);
                resolve(value);
            };
            const onAbort = () => done(false);
            this.waiters.add(done);
            if (signal?.aborted)
                onAbort();
            else
                signal?.addEventListener('abort', onAbort, { once: true });
        });
    }

    async drainAndClose(timeoutMs = 2_000) {
        this.accepting = false;
        this.#pump();
        let drained = this.inflight === 0 && this.queue.size === 0;
        if (!drained) {
            drained = await new Promise((resolve) => {
                const done = (value) => { clearTimeout(timer); this.waiters.delete(done); resolve(value); };
                const timer = setTimeout(() => done(false), timeoutMs);
                this.waiters.add(done);
            });
        }
        if (!drained)
            this.queue.clear();
        if (!this.closed) {
            this.closed = true;
            try {
                if (this.used)
                    await new Promise((resolve) => this.socket.close(resolve));
            } catch {
                // An unbound/already closed datagram socket needs no further cleanup.
            }
        }
        return drained;
    }
}

export class TimedDispatcher {
    constructor(sender, maxQueue = DEFAULT_MAX_QUEUE) {
        this.sender = sender;
        this.maxQueue = integerInRange(maxQueue, 'max queue', 1, 262_144);
        this.heap = [];
        this.sequence = 0;
        this.timer = null;
        this.timerDueNs = null;
        this.waiters = new Set();
        this.dropped = 0;
    }

    schedule(item, delayMs = 0) {
        const entry = {
            item,
            priority: item.priority ?? isLifecycleAddress(item.address),
            dueNs: process.hrtime.bigint() + BigInt(Math.round(Math.max(0, delayMs) * 1_000_000)),
            sequence: this.sequence++
        };
        if (this.heap.length >= this.maxQueue) {
            if (!entry.priority) {
                ++this.dropped;
                return false;
            }
            const motionIndex = this.heap.findIndex((candidate) => !candidate.priority);
            if (motionIndex < 0) {
                ++this.dropped;
                return false;
            }
            this.#removeAt(motionIndex);
            ++this.dropped;
        }
        this.#push(entry);
        this.#arm();
        return true;
    }

    flushNow() {
        if (this.timer) {
            clearTimeout(this.timer);
            this.timer = null;
            this.timerDueNs = null;
        }
        while (this.heap.length > 0)
            this.sender.enqueue(this.#pop().item);
        this.#notifyEmpty();
    }

    clear() {
        if (this.timer) {
            clearTimeout(this.timer);
            this.timer = null;
            this.timerDueNs = null;
        }
        this.heap.length = 0;
        this.#notifyEmpty();
    }

    async waitUntilEmpty(signal) {
        if (this.heap.length === 0)
            return;
        await new Promise((resolve, reject) => {
            const onAbort = () => {
                this.waiters.delete(onEmpty);
                reject(new AbortError());
            };
            const onEmpty = () => {
                signal?.removeEventListener('abort', onAbort);
                resolve();
            };
            this.waiters.add(onEmpty);
            if (signal?.aborted)
                onAbort();
            else
                signal?.addEventListener('abort', onAbort, { once: true });
        });
    }

    #less(left, right) {
        return left.dueNs < right.dueNs
            || (left.dueNs === right.dueNs && left.sequence < right.sequence);
    }

    #push(entry) {
        this.heap.push(entry);
        let index = this.heap.length - 1;
        while (index > 0) {
            const parent = (index - 1) >> 1;
            if (!this.#less(this.heap[index], this.heap[parent]))
                break;
            [this.heap[index], this.heap[parent]] = [this.heap[parent], this.heap[index]];
            index = parent;
        }
    }

    #pop() {
        const first = this.heap[0];
        const last = this.heap.pop();
        if (this.heap.length > 0) {
            this.heap[0] = last;
            this.#siftDown(0);
        }
        return first;
    }

    #removeAt(index) {
        const last = this.heap.pop();
        if (index >= this.heap.length)
            return;
        this.heap[index] = last;
        if (index > 0 && this.#less(this.heap[index], this.heap[(index - 1) >> 1])) {
            while (index > 0) {
                const parent = (index - 1) >> 1;
                if (!this.#less(this.heap[index], this.heap[parent]))
                    break;
                [this.heap[index], this.heap[parent]] = [this.heap[parent], this.heap[index]];
                index = parent;
            }
        } else {
            this.#siftDown(index);
        }
    }

    #siftDown(start) {
        let index = start;
        for (;;) {
            const left = index * 2 + 1;
            const right = left + 1;
            let smallest = index;
            if (left < this.heap.length && this.#less(this.heap[left], this.heap[smallest]))
                smallest = left;
            if (right < this.heap.length && this.#less(this.heap[right], this.heap[smallest]))
                smallest = right;
            if (smallest === index)
                break;
            [this.heap[index], this.heap[smallest]] = [this.heap[smallest], this.heap[index]];
            index = smallest;
        }
    }

    #arm() {
        if (this.heap.length === 0)
            return;
        if (this.timer && this.timerDueNs <= this.heap[0].dueNs)
            return;
        if (this.timer)
            clearTimeout(this.timer);
        this.timerDueNs = this.heap[0].dueNs;
        const remainingNs = this.heap[0].dueNs - process.hrtime.bigint();
        const delayMs = remainingNs <= 0n
            ? 0
            : Math.min(2_147_483_647, Number(remainingNs / 1_000_000n));
        this.timer = setTimeout(() => {
            this.timer = null;
            this.timerDueNs = null;
            const now = process.hrtime.bigint();
            while (this.heap.length > 0 && this.heap[0].dueNs <= now)
                this.sender.enqueue(this.#pop().item);
            this.#notifyEmpty();
            this.#arm();
        }, delayMs);
    }

    #notifyEmpty() {
        if (this.heap.length !== 0)
            return;
        for (const resolve of this.waiters)
            resolve();
        this.waiters.clear();
    }
}

function generatorValue(seedHash, zoneIndex, source, tick, axis) {
    const seedPhase = (seedHash % 10_000) / 10_000;
    const phase = seedPhase * Math.PI * 2
        + zoneIndex * 0.613 + source * 0.173 + tick * 0.071;
    const value = axis === 'u'
        ? 0.5 + 0.5 * Math.sin(phase)
        : 0.5 + 0.5 * Math.cos(phase * 0.79 + 0.37);
    return Math.max(0, Math.min(1, value));
}

export function buildLifecyclePackets({ zones = ['A'], sourceStart = 0, sourceCount = 1,
                                        finger = 0, state = 'on' } = {}) {
    assertCondition(Array.isArray(zones) && zones.length > 0,
                    'zones must be a non-empty array');
    assertCondition(state === 'on' || state === 'off', 'lifecycle state must be on or off');
    const packets = [];
    for (const zone of zones) {
        for (let source = sourceStart; source < sourceStart + sourceCount; ++source) {
            const address = `/cs/${zone}/${source}/finger${finger}/${state}`;
            packets.push({
                zone,
                source,
                kind: 'lifecycle',
                priority: true,
                address,
                payload: encodeOscMessage(address, state === 'on' ? [{ type: 'i', value: 1 }] : [])
            });
        }
    }
    return packets;
}

export function buildProductionFrame({ zones = ['A'], sourceStart = 0, sourceCount = 1,
                                       finger = 0, tick = 0,
                                       seed = 'cosmic-chaos-lab', includeOn = false } = {}) {
    const packets = includeOn
        ? buildLifecyclePackets({ zones, sourceStart, sourceCount, finger, state: 'on' })
        : [];
    const seedHash = hashSeed(seed);
    zones.forEach((zone, zoneIndex) => {
        for (let source = sourceStart; source < sourceStart + sourceCount; ++source) {
            for (const axis of ['u', 'v']) {
                const address = `/cs/${zone}/${source}/finger${finger}/${axis}`;
                const value = generatorValue(seedHash, zoneIndex, source, tick, axis);
                packets.push({
                    zone,
                    source,
                    kind: 'motion',
                    priority: false,
                    address,
                    value,
                    payload: encodeOscMessage(address, [{ type: 'f', value }])
                });
            }
        }
    });
    return packets;
}

function parseZones(value) {
    const zones = String(value).split(',').map((zone) => zone.trim().toUpperCase());
    if (zones.length === 0 || zones.some((zone) => !/^[A-Z]$/.test(zone)))
        throw new CliError('zones must be a comma-separated list of letters A..Z');
    if (new Set(zones).size !== zones.length)
        throw new CliError('zones must not contain duplicates');
    return zones;
}

function tokenizeOptions(argv) {
    const values = new Map();
    const booleans = new Set(['help', 'h', 'loop', 'overwrite', 'quiet']);
    for (let index = 0; index < argv.length; ++index) {
        const token = argv[index];
        if (!token.startsWith('--') && token !== '-h')
            throw new CliError(`unexpected positional argument: ${token}`);
        const normalized = token === '-h' ? '--help' : token;
        const equals = normalized.indexOf('=');
        const name = normalized.slice(2, equals < 0 ? undefined : equals);
        if (!name)
            throw new CliError('empty option name');
        if (booleans.has(name)) {
            if (equals >= 0)
                throw new CliError(`--${name} does not take a value`);
            if (values.has(name))
                throw new CliError(`--${name} was provided more than once`);
            values.set(name, true);
            continue;
        }
        const value = equals >= 0 ? normalized.slice(equals + 1) : argv[++index];
        if (value === undefined || value.startsWith('--'))
            throw new CliError(`--${name} requires a value`);
        if (name === 'map') {
            const previous = values.get(name) ?? [];
            previous.push(value);
            values.set(name, previous);
        } else {
            if (values.has(name))
                throw new CliError(`--${name} was provided more than once`);
            values.set(name, value);
        }
    }
    return values;
}

const COMMON_VALUE_OPTIONS = new Set([
    'seed', 'drop', 'duplicate', 'reorder', 'jitter', 'burst', 'max-queue', 'max-inflight'
]);

function ensureAllowed(values, allowed) {
    for (const name of values.keys())
        if (!allowed.has(name) && name !== 'help' && name !== 'h')
            throw new CliError(`--${name} is not valid for this mode`);
}

function chaosFromValues(values) {
    return normalizeChaosOptions({
        seed: values.get('seed') ?? 'cosmic-chaos-lab',
        drop: values.get('drop') ?? 0,
        duplicate: values.get('duplicate') ?? 0,
        reorder: values.get('reorder') ?? 0,
        jitter: values.get('jitter') ?? 0,
        burst: values.get('burst') ?? '0'
    });
}

export function parseCli(argv) {
    if (argv.length === 0)
        throw new CliError('mode is required (proxy, replay, or generate)');
    if (argv[0] === '--help' || argv[0] === '-h')
        return { mode: 'help' };
    const mode = argv[0];
    if (!['proxy', 'replay', 'generate'].includes(mode))
        throw new CliError(`unknown mode: ${mode}`);
    const values = tokenizeOptions(argv.slice(1));
    if (values.get('help'))
        return { mode: 'help' };

    const commonAllowed = new Set([...COMMON_VALUE_OPTIONS, 'quiet']);
    const common = {
        chaos: chaosFromValues(values),
        maxQueue: integerInRange(values.get('max-queue') ?? DEFAULT_MAX_QUEUE,
                                 'max queue', 1, 262_144),
        maxInflight: integerInRange(values.get('max-inflight') ?? DEFAULT_MAX_INFLIGHT,
                                    'max inflight', 1, 4_096),
        quiet: Boolean(values.get('quiet'))
    };
    if (common.chaos.reorderWindow > common.maxQueue)
        throw new CliError('reorder window must not exceed max queue');

    if (mode === 'proxy') {
        ensureAllowed(values, new Set([...commonAllowed, 'map', 'capture', 'overwrite']));
        const capture = values.get('capture');
        if (!capture || capture.includes('\0'))
            throw new CliError('--capture PATH is required');
        return {
            mode,
            ...common,
            mappings: buildMappingTable(values.get('map')),
            capture,
            overwrite: Boolean(values.get('overwrite'))
        };
    }

    if (mode === 'replay') {
        ensureAllowed(values, new Set([...commonAllowed, 'map', 'input', 'speed', 'loop']));
        const input = values.get('input');
        if (!input || input.includes('\0'))
            throw new CliError('--input PATH is required');
        return {
            mode,
            ...common,
            mappings: buildMappingTable(values.get('map')),
            input,
            speed: numberInRange(values.get('speed') ?? 1, 'speed', 0.25, 16),
            loop: Boolean(values.get('loop'))
        };
    }

    ensureAllowed(values, new Set([
        ...commonAllowed, 'target', 'zones', 'zone-count', 'sources', 'source-start',
        'finger', 'hz', 'duration-ms'
    ]));
    if (!values.get('target'))
        throw new CliError('--target HOST:PORT is required');
    if (values.has('zones') && values.has('zone-count'))
        throw new CliError('--zones and --zone-count are mutually exclusive');
    const zones = values.has('zone-count')
        ? Array.from({ length: integerInRange(values.get('zone-count'), 'zone count', 1, 26) },
                     (_, index) => String.fromCharCode(65 + index))
        : parseZones(values.get('zones') ?? 'A');
    const sourceCount = integerInRange(values.get('sources') ?? 16, 'sources', 1, 256);
    const sourceStart = integerInRange(values.get('source-start') ?? 0, 'source start', 0, 255);
    if (sourceStart + sourceCount > 256)
        throw new CliError('source-start + sources must not exceed 256');
    if (common.maxQueue < zones.length * sourceCount)
        throw new CliError('max queue must fit at least one On/Off per generated zone/source');
    return {
        mode,
        ...common,
        target: parseHostPort(values.get('target')),
        zones,
        sourceCount,
        sourceStart,
        finger: integerInRange(values.get('finger') ?? 0, 'finger', 0, 9),
        hz: numberInRange(values.get('hz') ?? 30, 'hz', 0.1, 1_000),
        durationMs: values.has('duration-ms')
            ? numberInRange(values.get('duration-ms'), 'duration-ms', 1, 86_400_000)
            : null
    };
}

export function formatHelp() {
    return `Cosmic Microwave v2.8.0 Capture/Replay Chaos Lab

Usage:
  cosmic-chaos-lab.mjs proxy   --map LISTEN=HOST:PORT [--map ...] --capture FILE [options]
  cosmic-chaos-lab.mjs replay  --input FILE --map RECORDED=HOST:PORT [--map ...] [options]
  cosmic-chaos-lab.mjs generate --target HOST:PORT [--zones A,B | --zone-count N] [options]

Chaos options (all deterministic for the same --seed):
  --seed TEXT          Seed (default: cosmic-chaos-lab)
  --drop 0..1          Independent packet loss probability
  --duplicate 0..1     Packet duplication probability
  --reorder N          Shuffle within bounded windows of N packets (0 disables)
  --jitter MS          Uniform additional delay in 0..MS
  --burst P:N          Probability P starts an N-packet loss burst (0 disables)

Proxy/capture:
  --map 7000=127.0.0.1:6060   Listen/forward mapping; repeat for more ports
  --capture FILE               NDJSON output; existing files fail closed
  --overwrite                  Explicitly allow replacing the capture file

Replay:
  --speed 0.25..16      Timing multiplier (default: 1)
  --loop                 Reopen and replay the file until interrupted

Generate:
  --sources N           Consecutive sources (default: 16, max: 256)
  --source-start N      First source id (default: 0)
  --hz RATE             U/V frame rate (default: 30)
  --finger N            Production finger id (default: 0)
  --duration-ms MS      Stop automatically; otherwise run until interrupted

Capacity/options:
  --max-queue N         Bounded user-space queue (default: 8192)
  --max-inflight N      Outstanding UDP sends (default: 64)
  --quiet               Suppress status/summary output
`;
}

class AbortError extends Error {
    constructor() {
        super('operation aborted');
        this.name = 'AbortError';
    }
}

function waitForAbort(signal) {
    if (signal.aborted)
        return Promise.resolve();
    return new Promise((resolve) => signal.addEventListener('abort', resolve, { once: true }));
}

function waitMilliseconds(milliseconds, signal) {
    if (signal?.aborted)
        return Promise.reject(new AbortError());
    return new Promise((resolve, reject) => {
        const timer = setTimeout(done, Math.max(0, milliseconds));
        function done() {
            signal?.removeEventListener('abort', aborted);
            resolve();
        }
        function aborted() {
            clearTimeout(timer);
            reject(new AbortError());
        }
        signal?.addEventListener('abort', aborted, { once: true });
    });
}

async function waitUntil(targetNs, signal) {
    for (;;) {
        const remaining = targetNs - process.hrtime.bigint();
        if (remaining <= 0n)
            return;
        await waitMilliseconds(Math.min(1_000, Number(remaining / 1_000_000n)), signal);
    }
}

function dispatchOutputs(outputs, dispatcher) {
    for (const output of outputs)
        dispatcher.schedule(output.packet, output.delayMs);
}

function bindUdp(socket, port) {
    return new Promise((resolve, reject) => {
        const onError = (error) => { socket.off('listening', onListening); reject(error); };
        const onListening = () => { socket.off('error', onError); resolve(); };
        socket.once('error', onError);
        socket.once('listening', onListening);
        socket.bind(port, '0.0.0.0');
    });
}

function closeUdp(socket) {
    return new Promise((resolve) => {
        try { socket.close(resolve); } catch { resolve(); }
    });
}

function summaryLine(label, values) {
    return `[chaos-lab] ${label} ${Object.entries(values)
        .map(([key, value]) => `${key}=${value}`).join(' ')}`;
}

export async function runProxy(options, { signal, log = console.error } = {}) {
    let runtimeError = null;
    let resolveRuntimeFailure;
    const runtimeFailure = new Promise((resolve) => { resolveRuntimeFailure = resolve; });
    const fail = (error) => {
        if (!runtimeError) {
            runtimeError = error;
            resolveRuntimeFailure();
        }
    };
    const writer = new BoundedNdjsonWriter(options.capture, {
        maxQueue: options.maxQueue,
        overwrite: options.overwrite
    });
    await writer.ready();
    writer.stream.on('error', fail);
    const sender = new BoundedDatagramSender(options);
    const dispatcher = new TimedDispatcher(sender, options.maxQueue);
    const chaos = new ChaosPolicy(options.chaos);
    const sockets = [];
    const captureStart = process.hrtime.bigint();
    let idleFlush = null;

    try {
        for (const mapping of options.mappings.values()) {
            const socket = dgram.createSocket('udp4');
            socket.on('message', (payload, remote) => {
                try {
                    writer.write(createCaptureRecord({
                        startMonoNs: captureStart,
                        port: mapping.listenPort,
                        remote,
                        payload
                    }));
                    const packet = {
                        host: mapping.host,
                        port: mapping.port,
                        address: extractOscAddress(payload),
                        payload
                    };
                    dispatchOutputs(chaos.accept(packet), dispatcher);
                    if (idleFlush)
                        clearTimeout(idleFlush);
                    if (chaos.bufferedCount > 0) {
                        idleFlush = setTimeout(() => {
                            idleFlush = null;
                            dispatchOutputs(chaos.flush(), dispatcher);
                        }, REORDER_IDLE_FLUSH_MS);
                    }
                } catch (error) {
                    fail(error);
                }
            });
            sockets.push(socket);
            await bindUdp(socket, mapping.listenPort);
            socket.on('error', fail);
        }
        if (!options.quiet)
            log(`[chaos-lab] proxy listening on ${[...options.mappings.keys()].join(', ')}; capture=${options.capture}`);
        await Promise.race([waitForAbort(signal), runtimeFailure]);
    } finally {
        if (idleFlush)
            clearTimeout(idleFlush);
        await Promise.all(sockets.map(closeUdp));
        dispatchOutputs(chaos.flush(), dispatcher);
        dispatcher.flushNow();
        const drained = await sender.drainAndClose();
        await writer.close();
        if (!options.quiet)
            log(summaryLine('proxy stopped', {
                captured: writer.written,
                captureDropped: writer.dropped,
                forwarded: sender.sent,
                sendErrors: sender.sendErrors,
                chaosDropped: chaos.stats.droppedPackets,
                queueDropped: sender.queue.droppedMotion + sender.queue.droppedLifecycle + dispatcher.dropped,
                drained
            }));
    }
    if (runtimeError)
        throw runtimeError;
}

export async function runReplay(options, { signal, log = console.error } = {}) {
    const sender = new BoundedDatagramSender(options);
    const dispatcher = new TimedDispatcher(sender, options.maxQueue);
    let recordsRead = 0;
    let loops = 0;
    try {
        do {
            const chaos = new ChaosPolicy(options.chaos);
            const loopStart = process.hrtime.bigint();
            let previousDelta = -1;
            let loopRecords = 0;
            const speedUnits = BigInt(Math.round(options.speed * 1_000_000));
            for await (const record of readCaptureRecords(options.input, { signal })) {
                if (record.deltaMicros < previousDelta)
                    throw new Error('capture deltaMicros values must be monotonic');
                previousDelta = record.deltaMicros;
                const mapping = resolveMapping(options.mappings, record.port);
                const delayNs = BigInt(record.deltaMicros) * 1_000n * 1_000_000n / speedUnits;
                await waitUntil(loopStart + delayNs, signal);
                dispatchOutputs(chaos.accept({
                    host: mapping.host,
                    port: mapping.port,
                    address: record.address,
                    payload: Buffer.from(record.payload, 'base64')
                }), dispatcher);
                ++loopRecords;
                ++recordsRead;
            }
            if (loopRecords === 0)
                throw new Error('capture contains no records');
            dispatchOutputs(chaos.flush(), dispatcher);
            await dispatcher.waitUntilEmpty(signal);
            if (!await sender.waitUntilDrained(signal))
                throw new AbortError();
            ++loops;
            if (!options.quiet)
                log(summaryLine('replay pass', { pass: loops, records: loopRecords }));
        } while (options.loop && !signal.aborted);
    } catch (error) {
        if (!(error instanceof AbortError) || !signal.aborted)
            throw error;
    } finally {
        dispatcher.flushNow();
        const drained = await sender.drainAndClose();
        if (!options.quiet)
            log(summaryLine('replay stopped', {
                passes: loops,
                records: recordsRead,
                sent: sender.sent,
                sendErrors: sender.sendErrors,
                drained
            }));
    }
}

export async function runGenerate(options, { signal, log = console.error } = {}) {
    const sender = new BoundedDatagramSender(options);
    const dispatcher = new TimedDispatcher(sender, options.maxQueue);
    const chaos = new ChaosPolicy(options.chaos);
    const started = process.hrtime.bigint();
    const intervalNs = BigInt(Math.round(1_000_000_000 / options.hz));
    let tick = 0;
    let frames = 0;
    let generated = 0;

    const submit = (packets) => {
        for (const packet of packets) {
            ++generated;
            dispatchOutputs(chaos.accept({ ...packet, ...options.target }), dispatcher);
        }
    };

    try {
        if (!options.quiet)
            log(`[chaos-lab] generator ${options.zones.join(',')} x ${options.sourceCount} sources @ ${options.hz} Hz -> ${options.target.host}:${options.target.port}`);
        for (;;) {
            const elapsedMs = Number(process.hrtime.bigint() - started) / 1_000_000;
            if (signal.aborted || (options.durationMs !== null && elapsedMs >= options.durationMs))
                break;
            submit(buildProductionFrame({
                zones: options.zones,
                sourceStart: options.sourceStart,
                sourceCount: options.sourceCount,
                finger: options.finger,
                tick,
                seed: options.chaos.seed,
                includeOn: tick === 0
            }));
            ++frames;
            ++tick;
            const elapsedTicks = Number((process.hrtime.bigint() - started) / intervalNs);
            if (elapsedTicks > tick)
                tick = elapsedTicks;
            try {
                await waitUntil(started + BigInt(tick) * intervalNs, signal);
            } catch (error) {
                if (!(error instanceof AbortError))
                    throw error;
                break;
            }
        }
    } finally {
        // Do not allow delayed/reordered motion or On packets to overtake the
        // shutdown release. In-flight sends cannot be cancelled, so replacing
        // the entire queued tail with Off guarantees an eventual release.
        chaos.discard();
        dispatcher.clear();
        sender.clearQueued();
        for (const packet of buildLifecyclePackets({
            zones: options.zones,
            sourceStart: options.sourceStart,
            sourceCount: options.sourceCount,
            finger: options.finger,
            state: 'off'
        }))
            sender.enqueue({ ...packet, ...options.target });
        const drained = await sender.drainAndClose();
        if (!options.quiet)
            log(summaryLine('generator stopped', {
                frames,
                generated,
                sent: sender.sent,
                motionDropped: sender.queue.droppedMotion + dispatcher.dropped,
                lifecycleDropped: sender.queue.droppedLifecycle,
                drained
            }));
    }
}

export async function main(argv = process.argv.slice(2)) {
    let options;
    try {
        options = parseCli(argv);
    } catch (error) {
        console.error(`error: ${error.message}\n\n${formatHelp()}`);
        return 2;
    }
    if (options.mode === 'help') {
        console.log(formatHelp());
        return 0;
    }

    const controller = new AbortController();
    let signalExitCode = 0;
    const onSigint = () => { signalExitCode = 130; controller.abort(); };
    const onSigterm = () => { signalExitCode = 143; controller.abort(); };
    process.once('SIGINT', onSigint);
    process.once('SIGTERM', onSigterm);
    try {
        if (options.mode === 'proxy')
            await runProxy(options, { signal: controller.signal });
        else if (options.mode === 'replay')
            await runReplay(options, { signal: controller.signal });
        else
            await runGenerate(options, { signal: controller.signal });
        return signalExitCode;
    } catch (error) {
        console.error(`error: ${error.message}`);
        return 1;
    } finally {
        process.off('SIGINT', onSigint);
        process.off('SIGTERM', onSigterm);
    }
}

const invokedPath = process.argv[1] ? pathToFileURL(process.argv[1]).href : '';
if (invokedPath === import.meta.url)
    process.exitCode = await main();
