#include "../Source/CrowdSimulatorModel.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
    CrowdSimulatorModel::Profile parseProfile (const std::string& value)
    {
        if (value == "human") return CrowdSimulatorModel::Profile::human;
        if (value == "dense") return CrowdSimulatorModel::Profile::dense;
        if (value == "stress") return CrowdSimulatorModel::Profile::stress;
        throw std::runtime_error ("profile must be human, dense or stress");
    }

    const char* suffix (CrowdSimulatorModel::EventType type)
    {
        switch (type)
        {
            case CrowdSimulatorModel::EventType::x:   return "u";
            case CrowdSimulatorModel::EventType::y:   return "v";
            case CrowdSimulatorModel::EventType::on:  return "on";
            case CrowdSimulatorModel::EventType::off: return "on";
        }
        return "on";
    }

    bool isCoordinate (CrowdSimulatorModel::EventType type)
    {
        return type == CrowdSimulatorModel::EventType::x
            || type == CrowdSimulatorModel::EventType::y;
    }
}

int main (int argc, char** argv)
{
    if (argc < 2 || argc > 6)
    {
        std::cerr << "Usage: GenerateCrowdSimulatorTrace OUTPUT.jsonl "
                     "[participants=10] [seconds=60] [human|dense|stress] [seed]\n";
        return 2;
    }

    try
    {
        const std::string outputPath = argv[1];
        const int participants = argc > 2 ? std::stoi (argv[2]) : 10;
        const double seconds = argc > 3 ? std::stod (argv[3]) : 60.0;
        const auto profile = argc > 4 ? parseProfile (argv[4])
                                      : CrowdSimulatorModel::Profile::human;
        const std::uint64_t seed = argc > 5 ? std::stoull (argv[5])
                                            : CrowdSimulatorModel::defaultSeed;

        if (participants < 1 || participants > CrowdSimulatorModel::maxParticipants)
            throw std::runtime_error ("participants must be between 1 and 256");
        if (! (seconds > 0.0 && seconds <= 3600.0))
            throw std::runtime_error ("seconds must be in (0, 3600]");

        std::ofstream output (outputPath, std::ios::out | std::ios::trunc);
        if (! output)
            throw std::runtime_error ("cannot open output file");

        CrowdSimulatorModel model (participants, seed);
        model.setProfile (profile);
        CrowdSimulatorModel::EventBuffer events;
        for (int i = 0; i < participants; ++i)
            model.addCrowdParticipant (events);

        output << "{\"kind\":\"session_start\",\"format_version\":1,"
                  "\"started_iso\":\"1970-01-01T00:00:00.000Z\","
                  "\"started_unix_ms\":0,\"recorder\":"
                  "\"Cosmic Microwave CrowdSimulatorModel\",\"profile\":\""
               << CrowdSimulatorModel::profileName (profile) << "\",\"seed\":"
               << seed << ",\"participants\":" << participants << "}\n";

        const int tickCount = static_cast<int> (std::ceil (
            seconds * 1000.0 / CrowdSimulatorModel::tickIntervalMs));
        std::uint64_t packetId = 0;
        std::uint64_t eventId = 0;

        const auto writePacket = [&] (std::size_t eventCount,
                                      std::uint64_t elapsedMs,
                                      const char* completionReason)
        {
            if (eventCount == 0)
                return;

            ++packetId;
            output << "{\"kind\":\"osc_packet\",\"format_version\":1,"
                      "\"packet\":" << packetId
                   << ",\"received_iso\":\"1970-01-01T00:00:00.000Z\","
                      "\"received_unix_ms\":" << elapsedMs
                   << ",\"elapsed_ms\":" << elapsedMs
                   << ",\"framing\":\"simulator_tick\",\"events\":[";

            for (std::size_t index = 0; index < eventCount; ++index)
            {
                const auto& event = events[index];
                if (index != 0)
                    output << ',';
                ++eventId;
                output << "{\"event\":" << eventId
                       << ",\"received_iso\":\"1970-01-01T00:00:00.000Z\","
                          "\"received_unix_ms\":" << elapsedMs
                       << ",\"elapsed_ms\":" << elapsedMs
                       << ",\"packet_offset_ms\":0,\"max_kind\":\"anything\","
                          "\"address\":\"/cs/A/" << event.sourceId
                       << "/finger0/" << suffix (event.type) << "\",\"args\":[";
                if (isCoordinate (event.type))
                    output << std::fixed << std::setprecision (2) << event.value;
                else
                    output << (event.type == CrowdSimulatorModel::EventType::on ? 1 : 0);
                output << "],\"arg_types_inferred\":[\""
                       << (isCoordinate (event.type) ? "float32_by_contract"
                                                    : "int32_by_contract")
                       << "\"]}";
            }

            output << "],\"complete\":true,\"completion_reason\":\""
                   << completionReason << "\",\"completed_unix_ms\":"
                   << elapsedMs << ",\"dispatch_span_ms\":0}\n";
        };

        for (int tick = 1; tick <= tickCount; ++tick)
        {
            const auto eventCount = model.advance (true, events);
            const auto elapsedMs = static_cast<std::uint64_t> (tick)
                                 * CrowdSimulatorModel::tickIntervalMs;
            writePacket (eventCount, elapsedMs, "simulator_tick");
        }

        // Captures are replay artifacts, so they must be lifecycle-closed even
        // when the requested duration ends in the middle of a long gesture.
        // CrowdSimulatorModel::clear emits exactly one ordered Off for every
        // active participant before resetting the fixed population. The packet
        // completion reason is the analyzer contract: these Offs remain real
        // replay traffic but are right-censored out of sampled hold/idle
        // distributions because EOF, rather than a participant, ended them.
        const auto endMs = static_cast<std::uint64_t> (tickCount)
                         * CrowdSimulatorModel::tickIntervalMs;
        writePacket (model.clear (events), endMs, "simulator_cleanup");

        output << "{\"kind\":\"session_end\",\"format_version\":1,"
                  "\"ended_iso\":\"1970-01-01T00:00:00.000Z\","
                  "\"ended_unix_ms\":" << endMs
               << ",\"duration_ms\":" << endMs
               << ",\"packets\":" << packetId << ",\"events\":" << eventId
               << "}\n";

        std::cout << "Wrote " << eventId << " events in " << packetId
                  << " packets to " << outputPath << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "GenerateCrowdSimulatorTrace: " << error.what() << '\n';
        return 1;
    }
}
