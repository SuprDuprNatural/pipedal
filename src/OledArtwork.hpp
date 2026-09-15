// Copyright (c) 2026. SPDX-License-Identifier: MIT
#pragma once

#include "json.hpp"
#include <algorithm>
#include <array>
#include <vector>

namespace pipedal
{
    // Preset JSON uses a bounded array, row-major, MSB first, 1 = lit.
    // Validate as integers before converting: json_reader::read(uint8_t*)
    // reads characters and must not be used for bitmap data.
    class OledArtwork : public JsonSerializable
    {
    public:
        static constexpr size_t Width = 128;
        static constexpr size_t Height = 40;
        static constexpr size_t RowBytes = Width / 8;
        static constexpr size_t ByteCount = RowBytes * Height;
        static constexpr size_t LegacyHeight = 24;
        static constexpr size_t LegacyByteCount = RowBytes * LegacyHeight;

        std::array<uint8_t, ByteCount> bytes{};

        void read_json(json_reader &reader) override
        {
            std::vector<uint8_t> values;
            values.reserve(ByteCount);
            try
            {
                reader.consume('[');
                while (reader.peek() != ']')
                {
                    if (!values.empty()) reader.consume(',');
                    if (values.size() == ByteCount)
                        throw JsonException("Too many bytes.");
                    int value;
                    reader.read(&value);
                    if (value < 0 || value > 255)
                        throw JsonException("Byte out of range.");
                    values.push_back(static_cast<uint8_t>(value));
                }
                reader.consume(']');
                if (values.size() != ByteCount && values.size() != LegacyByteCount)
                    throw JsonException("Wrong byte count.");
            }
            catch (const std::exception &)
            {
                throw JsonException(
                    "OLED artwork must contain exactly 640 integers from 0 to 255 "
                    "(legacy 384-byte artwork is also accepted).");
            }

            if (values.size() == LegacyByteCount)
            {
                // Stretch unreleased 128x24 artwork to the new full lower strip
                // so presets created during development remain readable.
                for (size_t y = 0; y < Height; ++y)
                {
                    const size_t sourceY = y * LegacyHeight / Height;
                    std::copy_n(values.begin() + sourceY * RowBytes, RowBytes,
                        bytes.begin() + y * RowBytes);
                }
            }
            else
            {
                std::copy(values.begin(), values.end(), bytes.begin());
            }
        }

        void write_json(json_writer &writer) const override
        {
            writer.write(std::vector<unsigned int>(bytes.begin(), bytes.end()));
        }
    };
}
