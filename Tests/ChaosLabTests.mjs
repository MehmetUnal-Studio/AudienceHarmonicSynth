import test from 'node:test';
import assert from 'node:assert/strict';

import {
    ChaosPolicy,
    buildLifecyclePackets,
    buildMappingTable,
    buildProductionFrame,
    createCaptureRecord,
    decodeOscMessage,
    encodeOscMessage,
    extractOscAddress,
    parseCaptureLine,
    parseCli,
    parseForwardMapping,
    resolveMapping,
    serializeCaptureRecord
} from '../tools/cosmic-chaos-lab.mjs';

function chaosTrace(seed) {
    const policy = new ChaosPolicy({
        seed,
        drop: 0.12,
        duplicate: 0.35,
        reorder: 5,
        jitter: 17,
        burst: '0.04:3'
    });
    const outputs = [];
    for (let id = 0; id < 80; ++id)
        outputs.push(...policy.accept({ id }));
    outputs.push(...policy.flush());
    return outputs.map(({ packet, duplicate, delayMs }) => ({
        id: packet.id,
        duplicate,
        delayMicros: Math.round(delayMs * 1_000)
    }));
}

test('chaos policy is deterministic for a seed', () => {
    const first = chaosTrace('venue-A-2026');
    const second = chaosTrace('venue-A-2026');
    const different = chaosTrace('venue-B-2026');

    assert.deepEqual(first, second);
    assert.notDeepEqual(first, different);
    assert.ok(first.length > 0);
});
test('reorder is a bounded window and preserves every packet', () => {
    const window = 4;
    const policy = new ChaosPolicy({ seed: 'bounded', reorder: window });
    const outputs = [];
    for (let id = 0; id < 19; ++id)
        outputs.push(...policy.accept({ id }));
    outputs.push(...policy.flush());

    assert.ok(policy.stats.maxReorderBuffered <= window);
    assert.deepEqual(outputs.map((entry) => entry.packet.id).sort((a, b) => a - b),
                     Array.from({ length: 19 }, (_, index) => index));

    for (let start = 0; start < 19; start += window) {
        const expected = Array.from({ length: Math.min(window, 19 - start) },
                                    (_, index) => start + index);
        const actual = outputs.slice(start, start + window)
            .map((entry) => entry.packet.id).sort((a, b) => a - b);
        assert.deepEqual(actual, expected, `window beginning at input ${start}`);
    }
});

test('capture NDJSON round-trips replay payload and timing metadata', () => {
    const payload = encodeOscMessage('/cs/C/42/finger0/u', [{ type: 'f', value: 0.625 }]);
    const record = createCaptureRecord({
        startMonoNs: 1_000_000_000n,
        nowMonoNs: 1_001_234_999n,
        wallTimeMs: Date.UTC(2026, 7, 23, 12, 34, 56, 789),
        port: 7_000,
        remote: { address: '127.0.0.1', port: 51_234 },
        payload
    });
    const line = serializeCaptureRecord(record);
    const replayRecord = parseCaptureLine(line.trimEnd());

    assert.equal(line.endsWith('\n'), true);
    assert.equal(replayRecord.deltaMicros, 1_234);
    assert.equal(replayRecord.wallTime, '2026-08-23T12:34:56.789Z');
    assert.equal(replayRecord.port, 7_000);
    assert.equal(replayRecord.address, '/cs/C/42/finger0/u');
    assert.equal(replayRecord.remoteAddress, '127.0.0.1');
    assert.equal(replayRecord.remotePort, 51_234);
    assert.deepEqual(Buffer.from(replayRecord.payload, 'base64'), payload);
    assert.throws(() => parseCaptureLine('{"v":1,"payload":"not base64"}'));
});

test('port mappings parse, resolve, and reject ambiguous input', () => {
    assert.deepEqual(parseForwardMapping('7000=127.0.0.1:6060'), {
        listenPort: 7_000,
        host: '127.0.0.1',
        port: 6_060
    });
    assert.deepEqual(parseForwardMapping('7001:localhost:6061'), {
        listenPort: 7_001,
        host: 'localhost',
        port: 6_061
    });

    const mappings = buildMappingTable([
        '7000=127.0.0.1:6060',
        '7001=localhost:6061'
    ]);
    assert.equal(resolveMapping(mappings, 7_001).port, 6_061);
    assert.throws(() => resolveMapping(mappings, 7_002), /no --map destination/);
    assert.throws(() => buildMappingTable([
        '7000=127.0.0.1:6060',
        '7000=127.0.0.1:6061'
    ]), /duplicate mapping/);
    assert.throws(() => parseForwardMapping('0=localhost:6060'), /listen port/);
    assert.throws(() => parseForwardMapping('7000=[::1]:6060'), /IPv6 is not supported/);
});

test('production generator emits exact OSC addresses and wire types', () => {
    const packets = buildProductionFrame({
        zones: ['A', 'C'],
        sourceStart: 7,
        sourceCount: 2,
        finger: 0,
        tick: 12,
        seed: 'wire-test',
        includeOn: true
    });

    assert.equal(packets.length, 12);
    assert.equal(packets.slice(0, 4).every((packet) => packet.priority), true);
    assert.equal(packets.slice(4).every((packet) => !packet.priority), true);
    assert.deepEqual(packets.slice(0, 4).map((packet) => packet.address), [
        '/cs/A/7/finger0/on',
        '/cs/A/8/finger0/on',
        '/cs/C/7/finger0/on',
        '/cs/C/8/finger0/on'
    ]);

    for (const packet of packets) {
        assert.equal(extractOscAddress(packet.payload), packet.address);
        const decoded = decodeOscMessage(packet.payload);
        assert.equal(decoded.address, packet.address);
        if (packet.kind === 'lifecycle')
            assert.deepEqual(decoded.args, [{ type: 'i', value: 1 }]);
        else {
            assert.equal(decoded.args.length, 1);
            assert.equal(decoded.args[0].type, 'f');
            assert.ok(decoded.args[0].value >= 0 && decoded.args[0].value <= 1);
            assert.ok(Math.abs(decoded.args[0].value - packet.value) < 1e-6);
        }
    }

    const releases = buildLifecyclePackets({
        zones: ['Z'], sourceStart: 255, sourceCount: 1, finger: 9, state: 'off'
    });
    assert.equal(releases[0].address, '/cs/Z/255/finger9/off');
    assert.deepEqual(decodeOscMessage(releases[0].payload).args, []);
});

test('OSC encoder uses padded big-endian int32 and float32 wire data', () => {
    const encoded = encodeOscMessage('/x', [
        { type: 'i', value: 0x01020304 },
        { type: 'f', value: 0.5 }
    ]);
    assert.equal(encoded.toString('hex'), '2f7800002c696600010203043f000000');
    assert.deepEqual(decodeOscMessage(encoded), {
        address: '/x',
        args: [
            { type: 'i', value: 0x01020304 },
            { type: 'f', value: 0.5 }
        ]
    });
});

test('CLI rejects unsafe or invalid arguments before opening sockets/files', () => {
    assert.throws(() => parseCli(['proxy', '--map', '7000=localhost:6060']),
                  /--capture PATH is required/);
    assert.throws(() => parseCli([
        'replay', '--input', 'take.ndjson', '--map', '7000=localhost:6060', '--speed', '16.01'
    ]), /speed/);
    assert.throws(() => parseCli([
        'generate', '--target', 'localhost:6060', '--source-start', '250', '--sources', '7'
    ]), /must not exceed 256/);
    assert.throws(() => parseCli([
        'generate', '--target', 'localhost:6060', '--unknown', 'x'
    ]), /not valid/);

    const valid = parseCli([
        'replay', '--input', 'take.ndjson', '--map', '7000=localhost:6060',
        '--speed', '0.25', '--loop', '--reorder', '8', '--burst', '0.1:3'
    ]);
    assert.equal(valid.speed, 0.25);
    assert.equal(valid.loop, true);
    assert.equal(valid.chaos.reorderWindow, 8);
    assert.deepEqual(valid.chaos.burst, { probability: 0.1, length: 3 });
});
