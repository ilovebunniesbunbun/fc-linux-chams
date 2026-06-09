#ifndef __KEYVALUE_3_H__
#define __KEYVALUE_3_H__

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
#	pragma once
#endif

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <istream>
#include <optional>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <memory>
#include <utility>
#include <variant>
#include <vector>
#include <span>

namespace KeyValue3
{
	
	class KVObject;
	class KVValue;

	struct Guid final
	{
		std::array<std::uint8_t, 16> bytes{};

		[[nodiscard]] static std::optional<Guid> TryParse(std::string_view s) noexcept;

		[[nodiscard]] std::string ToStringLower() const;

		[[nodiscard]] friend bool operator==(const Guid&, const Guid&) = default;
	};

	namespace detail
	{

		[[nodiscard]] constexpr int HexNibble(char c) noexcept
		{
			if (c >= '0' && c <= '9') return c - '0';
			if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
			if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
			return -1;
		}

		[[nodiscard]] constexpr std::optional<std::uint8_t> HexByte(std::string_view s, std::size_t i) noexcept
		{
			if (i + 1 >= s.size()) return std::nullopt;
			const int hi = HexNibble(s[i]);
			const int lo = HexNibble(s[i + 1]);
			if (hi < 0 || lo < 0) return std::nullopt;
			return static_cast<std::uint8_t>((hi << 4) | lo);
		}

	} 

	inline std::optional<Guid> Guid::TryParse(std::string_view s) noexcept
	{
		if (s.size() != 36) return std::nullopt;
		if (s[8] != '-' || s[13] != '-' || s[18] != '-' || s[23] != '-') return std::nullopt;

		Guid g{};
		auto& b = g.bytes;

		auto set = [&] (std::size_t byteIdx, std::size_t strPos) -> bool {
			if (strPos + 1 >= s.size()) return false;
			const int hi = detail::HexNibble(s[strPos]);
			const int lo = detail::HexNibble(s[strPos + 1]);
			if (hi < 0 || lo < 0) return false;
			b[byteIdx] = static_cast<std::uint8_t>((hi << 4) | lo);
			return true;
		};

		if (!set(0, 0) || !set(1, 2) || !set(2, 4) || !set(3, 6)) return std::nullopt;
		
		if (!set(4, 9) || !set(5, 11)) return std::nullopt;
		
		if (!set(6, 14) || !set(7, 16)) return std::nullopt;
		
		if (!set(8, 19) || !set(9, 21)) return std::nullopt;
		
		if (!set(10, 24) || !set(11, 26) || !set(12, 28) ||
			!set(13, 30) || !set(14, 32) || !set(15, 34)) return std::nullopt;

		return g;
	}

	inline std::string Guid::ToStringLower() const
	{
		const auto& b = bytes;
		char buf[37];
		std::snprintf(buf, sizeof(buf),
					  "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
					  b[0], b[1], b[2], b[3],
					  b[4], b[5],
					  b[6], b[7],
					  b[8], b[9],
					  b[10], b[11], b[12], b[13], b[14], b[15]);
		return buf;
	}

	struct KV3ID final
	{
		std::string name;
		Guid        id;

		[[nodiscard]] std::string ToString() const
		{
			return name + ":version{" + id.ToStringLower() + "}";
		}
	};

	class KV3IDLookup final
	{
	public:
		[[nodiscard]] static KV3ID                 Get(std::string_view name);
		[[nodiscard]] static KV3ID                 GetByValue(const Guid& value);
		[[nodiscard]] static std::optional<Guid>   TryGetGuid(std::string_view name);

	private:
		[[nodiscard]] static const std::unordered_map<std::string, Guid>& Table();
	};

	inline const std::unordered_map<std::string, Guid>& KV3IDLookup::Table()
	{
		static const std::unordered_map<std::string, Guid> t = [] {
			std::unordered_map<std::string, Guid> m;
			m.reserve(420);

			auto add = [&] (const char* name, std::array<std::uint8_t, 16> raw) {
				Guid g; g.bytes = raw;
				m.emplace(name, g);
			};

			add("animgraph0",  { 0x02,0x38,0xB3,0xF8,0x19,0x7E,0x64,0x41,0x9D,0xD5,0x2D,0x3C,0x9A,0x19,0x3C,0x22 });
			add("animgraph1",  { 0x4A,0x1B,0x50,0xED,0x4B,0x4C,0x32,0x48,0x91,0x7C,0x53,0x4F,0xE2,0x78,0x72,0x59 });
			add("animgraph2",  { 0x71,0x94,0x46,0x3D,0x5C,0x48,0x12,0x45,0x96,0x5F,0x80,0x9C,0x72,0xF7,0xCE,0xD7 });
			add("animgraph3",  { 0xA8,0xD7,0xD0,0xCC,0x97,0xF4,0x21,0x45,0x81,0x7A,0xFF,0x63,0x75,0xA1,0xB4,0xC9 });
			add("animgraph4",  { 0x39,0xCA,0x40,0x95,0x42,0xDF,0x13,0x45,0x88,0xD8,0x9F,0xCB,0xDB,0x31,0x49,0x61 });
			add("animgraph5",  { 0x24,0x8D,0x21,0x93,0x48,0xA8,0x6B,0x41,0x96,0xD9,0x87,0xC0,0xDC,0x19,0x37,0x57 });
			add("animgraph6",  { 0x91,0xF3,0xA7,0xAE,0x89,0x89,0x2C,0x4C,0xB1,0x4D,0x4F,0x9E,0x2A,0x01,0xE4,0xF1 });
			add("animgraph7",  { 0x18,0x16,0xAD,0xFA,0x99,0x22,0xB5,0x41,0x87,0x84,0x3E,0x61,0x75,0x1F,0x49,0x37 });
			add("animgraph8",  { 0xE4,0xB2,0x19,0x93,0x93,0x8C,0xCD,0x4F,0xB2,0xEF,0xDB,0x49,0x5C,0x7A,0x48,0x36 });
			add("animgraph9",  { 0xDF,0x45,0xC0,0xAF,0xCB,0x24,0xF3,0x4A,0xA1,0xBF,0xE9,0xF3,0x86,0x4B,0xB2,0x8A });
			add("animgraph10", { 0xBF,0xE1,0x9D,0xCC,0x44,0xB3,0x4C,0x4F,0x83,0x1C,0x61,0x7E,0xDC,0xCA,0x4D,0xAD });
			add("animgraph11", { 0xBA,0x52,0xCB,0x86,0x9A,0x6F,0x75,0x4D,0x8F,0xDB,0x87,0x23,0x51,0x9E,0x78,0x09 });
			add("animgraph12", { 0xDD,0xC6,0x9A,0xC0,0x0B,0x3C,0x6E,0x4F,0xB3,0x1B,0x7A,0xCD,0x0B,0x52,0xB2,0xB5 });
			add("animgraph13", { 0xAA,0xEB,0x1E,0xA5,0x98,0x1D,0x1B,0x45,0xA6,0x30,0x88,0x93,0xC8,0x1F,0x7E,0x4C });
			add("animgraph14", { 0xEF,0x37,0x7C,0xF2,0xD4,0x50,0x6B,0x41,0x95,0xA4,0x08,0x4C,0xED,0x7E,0x86,0x9D });
			add("animgraph15", { 0x5B,0xF9,0x8C,0x85,0xBC,0x68,0xD5,0x47,0xAB,0x3A,0x5D,0x50,0xEB,0xE3,0x22,0xDA });
			add("animgraph16", { 0x64,0x53,0xB0,0xD5,0xB0,0x63,0xF4,0x48,0x80,0xF5,0x58,0x54,0x1E,0x2F,0x36,0x95 });
			add("animgraph17", { 0x8C,0x80,0x57,0xAC,0x70,0xF4,0xD3,0x4F,0xB4,0x86,0xE5,0xF0,0xAC,0x9D,0x9E,0xE2 });
			add("animgraph18", { 0x94,0x83,0xE1,0x04,0xA9,0xC7,0x16,0x43,0x8B,0xCA,0x87,0x83,0x0B,0xB1,0xEB,0x6B });
			add("animgraph19", { 0xB7,0x35,0xDB,0x0A,0x85,0x25,0x02,0x43,0x8D,0x05,0xE2,0x82,0x5B,0x45,0x18,0xAC });
			add("animgraph20", { 0xD1,0x04,0xD0,0x57,0xB1,0xD6,0x16,0x40,0x83,0xFE,0x82,0x2E,0xF5,0x8C,0x50,0x98 });
			add("animgraph21", { 0x38,0x46,0xA6,0xD7,0x21,0x54,0x64,0x4D,0xAC,0x4A,0x9D,0xD3,0x83,0x17,0x30,0x0E });
			add("animgraph22", { 0xB7,0xB1,0xC5,0x92,0x5E,0x9F,0x18,0x42,0x88,0xBC,0x75,0x6D,0xCC,0xEE,0x84,0x72 });
			add("animgraph23", { 0x81,0x0A,0x6A,0x12,0x48,0x9C,0x8C,0x49,0xAE,0x7A,0xBA,0x0C,0xEF,0x3E,0xEA,0x1B });
			add("animgraph24", { 0xC3,0xE5,0x60,0x2E,0xF0,0xDE,0x8C,0x4F,0x85,0x78,0x47,0x66,0xC9,0x56,0x20,0x86 });
			add("animgraph25", { 0x20,0xB3,0x98,0xA2,0x44,0x73,0x7E,0x45,0x91,0x60,0x54,0xF2,0x29,0x30,0x35,0x09 });
			add("animgraph26", { 0x58,0xCF,0x16,0x6B,0x62,0x7F,0x20,0x48,0x9A,0x1E,0xCD,0x92,0xD1,0xCB,0xD3,0xFB });
			add("animgraph27", { 0x1E,0xFA,0x58,0xB0,0x40,0x3B,0x8F,0x47,0xB4,0x97,0xE4,0x62,0x3B,0x7B,0xFC,0x02 });
			add("animgraph28", { 0x38,0x08,0x2F,0xF1,0x23,0x08,0x47,0x4C,0xBE,0xA0,0x41,0x97,0x0D,0xAE,0x2B,0x24 });
			add("animgraph29", { 0x46,0x6C,0x55,0xD0,0xDF,0xF5,0xB1,0x48,0xA9,0xD1,0xBC,0x5E,0xAA,0x40,0x5F,0xE8 });
			add("animgraph30", { 0xB5,0x8F,0xA2,0x26,0x62,0x35,0x40,0x47,0x95,0xDA,0xEB,0x8C,0xF5,0x8A,0xB3,0x60 });
			add("animgraph31", { 0x73,0x99,0x56,0x14,0x3F,0x57,0xA7,0x43,0xA0,0xE2,0x0C,0x72,0x41,0xE1,0x99,0x25 });
			add("animgraph32", { 0xE8,0xA4,0xA5,0x9F,0x18,0xB8,0x38,0x47,0x95,0x06,0x25,0x3A,0x23,0x5B,0x03,0xAD });
			add("modeldoc0",   { 0x85,0x86,0x44,0xF9,0x73,0x40,0x8F,0x44,0x99,0x41,0x85,0xB9,0x8B,0x5B,0xC2,0x22 });
			add("modeldoc1",   { 0xB2,0x5D,0xCA,0x58,0xB9,0x6E,0xA2,0x48,0x90,0x6E,0x0C,0x22,0x70,0x41,0x33,0x51 });
			add("modeldoc2",   { 0x75,0x57,0x12,0xEE,0x94,0x3F,0xF8,0x48,0x8C,0xEF,0x4F,0x3C,0xE7,0x5F,0xEC,0x5C });
			add("modeldoc3",   { 0xAC,0xDA,0x48,0xAC,0x2E,0x56,0x83,0x4E,0x95,0x96,0x30,0xFB,0x51,0xA4,0x89,0x47 });
			add("modeldoc4",   { 0xE4,0xCE,0x74,0x51,0xFA,0x88,0x04,0x41,0xB1,0xB9,0xB9,0xF3,0x09,0xE5,0xC9,0xB1 });
			add("modeldoc5",   { 0x06,0x86,0x85,0xA2,0xE8,0x7E,0xBE,0x45,0x81,0xE3,0x3E,0xF9,0x94,0xCF,0x36,0x3E });
			add("modeldoc6",   { 0x41,0x56,0x1C,0xED,0x1E,0x54,0xB8,0x41,0xB1,0x4A,0x8A,0x78,0xF9,0x56,0xC2,0x61 });
			add("modeldoc7",   { 0x98,0x1B,0x91,0xCB,0xE6,0x4A,0x2F,0x4D,0x86,0xD6,0xFB,0x07,0x51,0x14,0x0E,0x0D });
			add("modeldoc8",   { 0x7B,0xB5,0xB2,0x75,0xC5,0x8E,0xED,0x45,0xA4,0x7B,0x59,0x49,0x4E,0x3F,0x83,0x05 });
			add("modeldoc9",   { 0x62,0x74,0x33,0x56,0xBC,0xC0,0x24,0x47,0xB5,0xD6,0xF5,0x53,0xDC,0x63,0xBF,0x25 });
			add("modeldoc10",  { 0x2F,0x75,0x43,0xCC,0x7F,0x59,0x05,0x49,0x8C,0x6E,0xD2,0x09,0x77,0x54,0x27,0x10 });
			add("modeldoc11",  { 0x31,0xA4,0xDC,0x15,0x9F,0x47,0x91,0x40,0xA7,0x9A,0x87,0x2C,0xF0,0x68,0xFD,0x97 });
			add("modeldoc12",  { 0x06,0xFF,0x2C,0x4E,0xAD,0x0D,0xD6,0x49,0xA7,0xD8,0xFA,0x18,0x58,0x78,0x8F,0x58 });
			add("modeldoc13",  { 0xCC,0xFD,0xAB,0x3F,0x03,0xB7,0x0E,0x4F,0xA1,0xD0,0x6D,0xA4,0x28,0xED,0x5B,0x90 });
			add("modeldoc14",  { 0x17,0x5E,0x8D,0x73,0x6A,0xC8,0x20,0x45,0xB5,0xAD,0x86,0x54,0x84,0x10,0x0D,0x16 });
			add("modeldoc15",  { 0x85,0x82,0xA3,0x35,0x04,0xD2,0x45,0x4A,0xBA,0x2B,0xBD,0x07,0xF4,0x93,0xE7,0xEE });
			add("modeldoc16",  { 0x6B,0x46,0xA6,0x4C,0x72,0x77,0x63,0x45,0xB0,0x5A,0x1F,0x24,0x18,0xD2,0xB9,0x42 });
			add("modeldoc17",  { 0xDC,0xED,0x61,0x93,0xE5,0xF0,0x77,0x4E,0x82,0x0A,0x43,0x77,0x66,0x0D,0xC1,0x85 });
			add("modeldoc18",  { 0x2E,0x59,0xAF,0xD7,0x02,0x66,0xCD,0x46,0xB5,0xEA,0x04,0xF1,0xC9,0xBA,0x89,0x5C });
			add("modeldoc19",  { 0xCE,0x8C,0xCD,0x3D,0x4B,0x49,0xB1,0x46,0x99,0x4E,0xF9,0xC0,0xB9,0xB4,0x19,0x9D });
			add("modeldoc20",  { 0xDC,0xE3,0x36,0xFF,0x22,0x92,0x0B,0x44,0xA4,0xD0,0xCF,0xB8,0x71,0x26,0xAB,0x1F });
			add("modeldoc21",  { 0xB6,0xE8,0xD8,0x75,0x3F,0x70,0x17,0x47,0xA1,0xEE,0x6D,0x58,0x72,0x34,0xFA,0xFF });
			add("modeldoc22",  { 0x39,0x70,0xC7,0x2F,0x46,0x11,0x2D,0x4B,0x88,0x4B,0xDD,0x87,0x99,0x58,0x7C,0x28 });
			add("modeldoc23",  { 0xFF,0x8B,0x6C,0x25,0x7C,0xDB,0xEA,0x48,0xA9,0xDC,0xCC,0x1B,0x82,0x91,0x8C,0x7E });
			add("modeldoc24",  { 0xC9,0x02,0xC6,0x3C,0x6F,0x06,0xEC,0x48,0x92,0xC4,0xB1,0x05,0xA6,0x43,0x95,0x3A });
			add("modeldoc25",  { 0x82,0xD9,0x7B,0x97,0x0B,0xF7,0x7E,0x45,0x85,0x16,0xEF,0x7C,0x87,0x9A,0x50,0xF9 });
			add("modeldoc26",  { 0x94,0x11,0xF7,0xD5,0x21,0xB2,0x76,0x4B,0x84,0x9E,0x88,0xB3,0x07,0x26,0x78,0x9A });
			add("modeldoc27",  { 0xB5,0x6E,0xF9,0x25,0xF1,0xE3,0xB5,0x43,0x83,0xB9,0x56,0xCD,0x5D,0x58,0xDF,0xE6 });
			add("modeldoc28",  { 0xCA,0xB6,0x63,0xFB,0x35,0xF4,0xA0,0x4A,0xA2,0xC7,0xC6,0x6D,0xDC,0x65,0x1D,0xCA });
			add("modeldoc29",  { 0x7C,0x42,0xEC,0x3C,0x0E,0x1B,0x48,0x4D,0xA9,0x0A,0x04,0x36,0xF3,0x3A,0x60,0x41 });
			add("modeldoc30",  { 0x0F,0x49,0xCD,0x66,0x7B,0x43,0x7D,0x4D,0xB0,0x09,0x79,0x07,0xD4,0x54,0x22,0xE2 });
			add("modeldoc31",  { 0xDD,0x48,0x5D,0x16,0xDC,0x8A,0x1D,0x4F,0xAB,0xF7,0x49,0x92,0xE0,0x4A,0xD5,0x77 });
			add("modeldoc32",  { 0x98,0xEF,0xDC,0xC5,0x29,0xB6,0xAB,0x46,0x88,0xE3,0xA1,0x7C,0x00,0x5C,0x93,0x5E });
			add("modeldoc33",  { 0x0A,0x39,0xD0,0xBE,0x64,0xA5,0xA6,0x41,0x9D,0xC3,0xC9,0xA2,0x93,0xC8,0xEE,0xA3 });
			add("modeldoc34",  { 0x24,0xF2,0xDC,0xEA,0xB0,0xF3,0x94,0x4E,0xA5,0x90,0x8A,0xBC,0x5D,0x9D,0x95,0xF7 });
			add("modeldoc35",  { 0xD0,0xEE,0x0E,0xB0,0x1D,0x97,0x07,0x43,0xA9,0x1E,0x31,0xE4,0x69,0x56,0xE8,0x94 });
			add("modeldoc36",  { 0xA4,0xAD,0x2D,0x97,0x28,0xB8,0xA4,0x45,0xBB,0x93,0x77,0x95,0xCF,0x05,0x85,0xDA });
			add("modeldoc37",  { 0xAA,0xB9,0x04,0x9E,0x73,0xF0,0x59,0x4E,0x89,0x23,0xFF,0x57,0x48,0x9E,0xB2,0xC7 });
			add("modeldoc38",  { 0x21,0x0C,0x52,0x81,0xAA,0x94,0xFA,0x45,0xA4,0x58,0x34,0x73,0x1C,0x14,0x59,0x69 });
			add("modeldoc39",  { 0x0D,0xCB,0xEE,0x1C,0x93,0x40,0x29,0x47,0xAB,0x18,0xB7,0x68,0x86,0xC6,0x4C,0xDE });
			add("modeldoc40",  { 0xF8,0xB1,0x0A,0xDA,0x22,0x97,0x10,0x49,0x94,0xB8,0x10,0xB6,0xA0,0x8C,0x09,0x34 });
			add("smartprop0",  { 0xED,0xAB,0xAE,0x31,0xC4,0x20,0x09,0x47,0xB6,0x40,0x99,0xDA,0x58,0x29,0x20,0xEA });
			add("smartprop1",  { 0xF0,0x56,0xB6,0x5A,0xDE,0x06,0x8A,0x47,0x80,0x4E,0x48,0x9E,0x82,0x99,0x4F,0xB5 });
			add("smartprop2",  { 0x6E,0xB6,0xDD,0x4D,0xED,0xC6,0xD0,0x4B,0xA4,0xEF,0xAF,0xB4,0x5A,0x02,0xAC,0xC2 });
			add("TestFormatA", { 0x13,0xFE,0x92,0x1D,0x51,0x3C,0x92,0x44,0x9F,0x22,0x96,0x2E,0x8C,0xA8,0x35,0x74 });
			add("TestFormatB", { 0x54,0xD9,0x9F,0x8C,0xD5,0x16,0xEC,0x4D,0x88,0x70,0x16,0xD0,0x41,0xE1,0x38,0x78 });
			add("TestFormatC", { 0x39,0x2C,0x37,0xBE,0x63,0x40,0xFA,0x45,0xA1,0x58,0xD6,0x17,0x07,0x5D,0x67,0x98 });
			add("TestFormatD", { 0xA8,0x5E,0xEE,0x56,0x54,0x7B,0x8B,0x4A,0xA9,0xFD,0xAC,0xA1,0x67,0x9B,0x40,0xDF });
			add("TestFormatE", { 0x5F,0x05,0x2A,0x62,0x81,0x47,0x4B,0x4E,0x9F,0x20,0xF3,0xA5,0x64,0x5B,0xE4,0xF4 });
			add("vcd1",        { 0x4B,0x2D,0x5B,0x6C,0x7B,0xAA,0x35,0x4D,0xA9,0xB0,0x36,0x48,0x4B,0x2A,0x0A,0xC5 });
			add("vcd2",        { 0x4E,0xE0,0x05,0xBC,0x21,0xC0,0xEA,0x4B,0xAC,0x1F,0x43,0xAB,0x77,0xA6,0xE3,0x5A });
			add("vcd3",		   { 0x32,0x8B,0x39,0xCA,0x6E,0xA8,0x62,0x49,0x9F,0xB8,0x12,0x9E,0xAE,0x57,0xFA,0xBF });
			add("generic",     { 0x7C,0x16,0x12,0x74,0xE9,0x06,0x98,0x46,0xAF,0xF2,0xE6,0x3E,0xB5,0x90,0x37,0xE7 });
			add("binary_auto", { 0xE6,0x09,0xB1,0x6E,0x85,0x6B,0x83,0x45,0xA3,0x12,0x70,0x3A,0x6E,0x04,0x06,0x8C });
			add("binary_bc",   { 0x46,0x1A,0x79,0x95,0xBC,0x95,0x6C,0x4F,0xA7,0x0B,0x05,0xBC,0xA1,0xB7,0xDF,0xD2 });
			add("binary_lz4",  { 0x8A,0x34,0x47,0x68,0xA1,0x63,0x5C,0x4F,0xA1,0x97,0x53,0x80,0x6F,0xD9,0xB1,0x19 });
			add("binary_zstd", { 0x00,0x0A,0x62,0x6F,0xF0,0xFE,0x05,0x43,0xA3,0x5F,0x04,0x23,0x46,0xB1,0xDB,0x29 });
			add("binary",      { 0x00,0x05,0x86,0x1B,0xD8,0xF7,0xC1,0x40,0xAD,0x82,0x75,0xA4,0x82,0x67,0xE7,0x14 });
			add("text",        { 0x3C,0x7F,0x1C,0xE2,0x33,0x8A,0xC5,0x41,0x99,0x77,0xA7,0x6D,0x3A,0x32,0xAA,0x0D });

			return m;
		}();
		return t;
	}

	inline std::optional<Guid> KV3IDLookup::TryGetGuid(std::string_view name)
	{
		const auto& t = Table();
		auto it = t.find(std::string(name));
		if (it == t.end()) return std::nullopt;
		return it->second;
	}

	inline KV3ID KV3IDLookup::Get(std::string_view name)
	{
		auto g = TryGetGuid(name);
		if (!g) throw std::invalid_argument(std::string("KV3ID not found: ") + std::string(name));
		return KV3ID{ std::string(name), *g };
	}

	inline KV3ID KV3IDLookup::GetByValue(const Guid& value)
	{
		for (const auto& [name, guid] : Table())
			if (guid == value) return KV3ID{ name, value };
		return KV3ID{ "vrfunknown", value };
	}

	class IndentedTextWriter final
	{
	public:
		explicit IndentedTextWriter(std::string indent_unit = "\t")
			: indent_unit_(std::move(indent_unit))
		{
		}

		IndentedTextWriter(const IndentedTextWriter&) = delete;
		IndentedTextWriter& operator=(const IndentedTextWriter&) = delete;
		IndentedTextWriter(IndentedTextWriter&&) = default;
		IndentedTextWriter& operator=(IndentedTextWriter&&) = default;

		void IndentPush() noexcept
		{
			++depth_;
		}
		void IndentPop()  noexcept
		{
			if (depth_) --depth_;
		}
		[[nodiscard]] std::size_t Depth() const noexcept
		{
			return depth_;
		}

		void Write(std::string_view s);
		void Write(char c);
		void Newline();
		void Writeln(std::string_view s = {});

		[[nodiscard]] std::string Str() const
		{
			return buf_.str();
		}

	private:
		std::ostringstream buf_;
		std::string        indent_unit_;
		std::size_t        depth_{ 0 };
		bool               at_line_start_{ true };

		void EmitIndent();
	};

	inline void IndentedTextWriter::EmitIndent()
	{
		if (!at_line_start_) return;
		for (std::size_t i = 0; i < depth_; ++i) buf_ << indent_unit_;
		at_line_start_ = false;
	}

	inline void IndentedTextWriter::Write(std::string_view s)
	{
		if (s.empty()) return;
		EmitIndent();
		buf_ << s;
	}

	inline void IndentedTextWriter::Write(char c)
	{
		EmitIndent();
		buf_ << c;
	}

	inline void IndentedTextWriter::Newline()
	{
		buf_ << '\n';
		at_line_start_ = true;
	}

	inline void IndentedTextWriter::Writeln(std::string_view s)
	{
		if (!s.empty()) Write(s);
		Newline();
	}

	enum class KVFlag : std::uint8_t
	{
		None = 0,
		Resource = 1,
		ResourceName = 2,
		Panorama = 3,
		SoundEvent = 4,
		SubClass = 5,
		EntityName = 6,
	};

	enum class KVType : std::uint8_t
	{
		Null = 0,
		Boolean,
		Int16,
		UInt16,
		Int32,
		UInt32,
		Int64,
		UInt64,
		Float,
		Double,
		String,
		Object,     
		Array,      
		BinaryBlob,
	};

	using BinaryBlob = std::vector<std::uint8_t>;

	using KVVariant = std::variant<
		std::monostate,                  
		bool,                            
		std::int16_t,                    
		std::uint16_t,                   
		std::int32_t,                    
		std::uint32_t,                   
		std::int64_t,                    
		std::uint64_t,                   
		float,                           
		double,                          
		std::string,                     
		std::shared_ptr<KVObject>,       
		BinaryBlob                       
	>;

	class KVValue final
	{
	public:
		KVType    type{ KVType::Null };
		KVFlag    flag{ KVFlag::None };
		KVVariant data{};

		[[nodiscard]] static KVValue make_null();
		[[nodiscard]] static KVValue make_bool(bool v);
		[[nodiscard]] static KVValue make_int64(std::int64_t v);
		[[nodiscard]] static KVValue make_double(double v);
		[[nodiscard]] static KVValue make_string(std::string v, KVFlag flag = KVFlag::None);
		[[nodiscard]] static KVValue make_object(KVObject&& obj, bool is_array);
		[[nodiscard]] static KVValue make_blob(BinaryBlob v);

		[[nodiscard]] bool is_null()   const noexcept
		{
			return type == KVType::Null;
		}
		[[nodiscard]] bool is_bool()   const noexcept
		{
			return type == KVType::Boolean;
		}
		[[nodiscard]] bool is_int()    const noexcept
		{
			return type == KVType::Int16 || type == KVType::Int32 || type == KVType::Int64;
		}
		[[nodiscard]] bool is_uint()   const noexcept
		{
			return type == KVType::UInt16 || type == KVType::UInt32 || type == KVType::UInt64;
		}
		[[nodiscard]] bool is_float()  const noexcept
		{
			return type == KVType::Float || type == KVType::Double;
		}
		[[nodiscard]] bool is_string() const noexcept
		{
			return type == KVType::String;
		}
		[[nodiscard]] bool is_object() const noexcept
		{
			return type == KVType::Object;
		}
		[[nodiscard]] bool is_array()  const noexcept
		{
			return type == KVType::Array;
		}
		[[nodiscard]] bool is_blob()   const noexcept
		{
			return type == KVType::BinaryBlob;
		}

		[[nodiscard]] bool               as_bool()   const;
		[[nodiscard]] std::int64_t       as_int64()  const;
		[[nodiscard]] float              as_float()  const;
		[[nodiscard]] double             as_double() const;
		[[nodiscard]] const std::string& as_string() const;
		[[nodiscard]] const BinaryBlob& as_blob()   const;
		[[nodiscard]] const KVObject& as_object() const;

		[[nodiscard]] const KVValue& operator[](std::string_view name) const;
		[[nodiscard]] const KVValue& operator[](std::size_t      index) const;
		
		[[nodiscard]] const KVValue& operator[](int index) const
		{
			return (*this)[static_cast<std::size_t>(index)];
		}

		void Serialize(IndentedTextWriter& w) const;
	};

	class KVObject final
	{
	public:
		using Item = std::pair<std::string, KVValue>;
		using Items = std::vector<Item>;

		std::optional<std::string> key;
		bool                       is_array{ false };

		explicit KVObject(std::optional<std::string> name = std::nullopt,
						  bool                       array = false,
						  std::size_t                reserve = 0);

		void set(std::string_view name, KVValue value);

		void push(KVValue value);

		[[nodiscard]] std::size_t     size()  const noexcept
		{
			return items_.size();
		}
		[[nodiscard]] bool            empty() const noexcept
		{
			return items_.empty();
		}
		[[nodiscard]] bool            has(std::string_view name) const noexcept;
		[[nodiscard]] const KVValue*  try_get(std::string_view name) const noexcept;
		[[nodiscard]] const KVValue&  at(std::string_view name) const;
		[[nodiscard]] const KVValue&  at(std::size_t index) const;

		[[nodiscard]] const KVValue& operator[](std::string_view name)  const
		{
			return at(name);
		}
		[[nodiscard]] const KVValue& operator[](std::size_t index) const
		{
			return at(index);
		}

		[[nodiscard]] Items::const_iterator begin()  const noexcept
		{
			return items_.begin();
		}
		[[nodiscard]] Items::const_iterator end()    const noexcept
		{
			return items_.end();
		}
		[[nodiscard]] std::span<const Item> items()  const noexcept
		{
			return { items_.data(), items_.size() };
		}

		void Serialize(IndentedTextWriter& w) const;

	private:
		Items											items_;
		std::unordered_map<std::string, std::size_t>	index_;

		void SerializeObject(IndentedTextWriter& w) const;
		void SerializeArray(IndentedTextWriter& w)  const;

		static void WriteKey(IndentedTextWriter& w, const std::string& key);
	};

	inline KVValue KVValue::make_null()
	{
		return KVValue{ KVType::Null, KVFlag::None, std::monostate{} };
	}
	inline KVValue KVValue::make_bool(bool v)
	{
		return KVValue{ KVType::Boolean, KVFlag::None, v };
	}
	inline KVValue KVValue::make_int64(std::int64_t v)
	{
		return KVValue{ KVType::Int64, KVFlag::None, v };
	}
	inline KVValue KVValue::make_double(double v)
	{
		return KVValue{ KVType::Double, KVFlag::None, v };
	}
	inline KVValue KVValue::make_string(std::string v, KVFlag flag)
	{
		return KVValue{ KVType::String, flag, std::move(v) };
	}
	inline KVValue KVValue::make_object(KVObject&& obj, bool is_array)
	{
		return KVValue{
			is_array ? KVType::Array : KVType::Object,
			KVFlag::None,
			std::make_shared<KVObject>(std::move(obj))
		};
	}
	inline KVValue KVValue::make_blob(BinaryBlob v)
	{
		return KVValue{ KVType::BinaryBlob, KVFlag::None, std::move(v) };
	}

	inline bool KVValue::as_bool() const
	{
		if (type != KVType::Boolean) throw std::runtime_error("KVValue: not a boolean");
		return std::get<bool>(data);
	}
	inline std::int64_t KVValue::as_int64() const
	{
		switch (type)
		{
		case KVType::Int16:  return std::get<std::int16_t>(data);
		case KVType::Int32:  return std::get<std::int32_t>(data);
		case KVType::Int64:  return std::get<std::int64_t>(data);
		case KVType::UInt16: return static_cast<std::int64_t>(std::get<std::uint16_t>(data));
		case KVType::UInt32: return static_cast<std::int64_t>(std::get<std::uint32_t>(data));
		case KVType::UInt64: return static_cast<std::int64_t>(std::get<std::uint64_t>(data));
		default: throw std::runtime_error("KVValue: not an integer type");
		}
	}
	inline float KVValue::as_float() const
	{
		if (type == KVType::Float)  return std::get<float>(data);
		if (type == KVType::Double) return static_cast<float>(std::get<double>(data));
		throw std::runtime_error("KVValue: not a floating-point type");
	}
	inline double KVValue::as_double() const
	{
		if (type == KVType::Float)  return static_cast<double>(std::get<float>(data));
		if (type == KVType::Double) return std::get<double>(data);
		throw std::runtime_error("KVValue: not a floating-point type");
	}
	inline const std::string& KVValue::as_string() const
	{
		if (type != KVType::String) throw std::runtime_error("KVValue: not a string");
		return std::get<std::string>(data);
	}
	inline const BinaryBlob& KVValue::as_blob() const
	{
		if (type != KVType::BinaryBlob) throw std::runtime_error("KVValue: not a binary blob");
		return std::get<BinaryBlob>(data);
	}
	inline const KVObject& KVValue::as_object() const
	{
		if (type != KVType::Object && type != KVType::Array)
			throw std::runtime_error("KVValue: not an object/array");
		return *std::get<std::shared_ptr<KVObject>>(data);
	}
	inline const KVValue& KVValue::operator[](std::string_view name) const
	{
		return as_object()[name];
	}
	inline const KVValue& KVValue::operator[](std::size_t index) const
	{
		return as_object()[index];
	}

	inline KVObject::KVObject(std::optional<std::string> name, bool array, std::size_t reserve)
		: key(std::move(name)), is_array(array)
	{
		items_.reserve(reserve);
		if (!array) index_.reserve(reserve);
	}

	inline void KVObject::set(std::string_view name, KVValue value)
	{
		if (is_array)
		{
			push(std::move(value));
			return;
		}

		auto it = std::find_if(items_.begin(), items_.end(),
							   [&] (const auto& item) { return item.first == name; });

		if (it == items_.end())
		{
			items_.emplace_back(name, std::move(value));
			index_.emplace(name, items_.size() - 1);
		}
		else
		{
			it->second = std::move(value);
		}
	}

	inline void KVObject::push(KVValue value)
	{
		if (!is_array) throw std::runtime_error("KVObject: push() on non-array");
		items_.emplace_back(std::to_string(items_.size()), std::move(value));
	}

	inline bool KVObject::has(std::string_view name) const noexcept
	{
		if (is_array) return false;
		return index_.find(std::string(name)) != index_.end();
	}

	inline const KVValue* KVObject::try_get(std::string_view name) const noexcept
	{
		if (is_array) return nullptr;
		auto it = index_.find(std::string(name));
		if (it == index_.end()) return nullptr;
		return &items_[it->second].second;
	}

	inline const KVValue& KVObject::at(std::string_view name) const
	{
		if (is_array) throw std::runtime_error("KVObject: at(name) on array");
		const KVValue* v = try_get(name);
		if (!v) throw std::out_of_range(std::string("KVObject: key not found: ") + std::string(name));
		return *v;
	}

	inline const KVValue& KVObject::at(std::size_t index) const
	{
		if (!is_array) throw std::runtime_error("KVObject: at(index) on non-array");
		if (index >= items_.size()) throw std::out_of_range("KVObject: index out of range");
		return items_[index].second;
	}

	namespace detail
	{

		inline void WriteEscapedString(IndentedTextWriter& w, const std::string& s)
		{
			w.Write('"');
			for (char c : s)
			{
				switch (c)
				{
				case '\n': w.Write("\\n");  break;
				case '\t': w.Write("\\t");  break;
				case '\\': w.Write("\\\\"); break;
				case '"':  w.Write("\\\""); break;
				default:   w.Write(c);      break;
				}
			}
			w.Write('"');
		}

		inline void WriteFloat(IndentedTextWriter& w, double v)
		{
			char buf[64];
			int n = std::snprintf(buf, sizeof(buf), "%.6f", v);
			if (n > 0) w.Write(std::string_view(buf, static_cast<std::size_t>(n)));
		}

		inline void WriteFlagPrefix(IndentedTextWriter& w, KVFlag flag)
		{
			switch (flag)
			{
			case KVFlag::None:         break;
			case KVFlag::Resource:     w.Write("resource:");     break;
			case KVFlag::ResourceName: w.Write("resource_name:"); break;
			case KVFlag::Panorama:     w.Write("panorama:");     break;
			case KVFlag::SoundEvent:   w.Write("soundevent:");   break;
			case KVFlag::SubClass:     w.Write("subclass:");     break;
			case KVFlag::EntityName:   w.Write("entity_name:");  break;
			default: throw std::runtime_error("KVFlag: unknown flag value");
			}
		}

		inline char UpperHex(int v) noexcept
		{
			v &= 0xF;
			return static_cast<char>(v < 10 ? '0' + v : 'A' + (v - 10));
		}

	}

	inline void KVValue::Serialize(IndentedTextWriter& w) const
	{
		detail::WriteFlagPrefix(w, flag);

		switch (type)
		{
		case KVType::Object:
		case KVType::Array:
			std::get<std::shared_ptr<KVObject>>(data)->Serialize(w);
			break;

		case KVType::String:
		{
			const auto& s = std::get<std::string>(data);
			if (s.find('\n') != std::string::npos)
			{
				w.Write("\"\"\"\n");
				w.Write(s);
				w.Write("\n\"\"\"");
			}
			else
			{
				detail::WriteEscapedString(w, s);
			}
			break;
		}
		case KVType::Boolean:
			w.Write(std::get<bool>(data) ? "true" : "false");
			break;
		case KVType::Float:
			detail::WriteFloat(w, static_cast<double>(std::get<float>(data)));
			break;
		case KVType::Double:
			detail::WriteFloat(w, std::get<double>(data));
			break;
		case KVType::Int64:
			w.Write(std::to_string(std::get<std::int64_t>(data)));
			break;
		case KVType::UInt64:
			w.Write(std::to_string(std::get<std::uint64_t>(data)));
			break;
		case KVType::Int32:
			w.Write(std::to_string(std::get<std::int32_t>(data)));
			break;
		case KVType::UInt32:
			w.Write(std::to_string(std::get<std::uint32_t>(data)));
			break;
		case KVType::Int16:
			w.Write(std::to_string(std::get<std::int16_t>(data)));
			break;
		case KVType::UInt16:
			w.Write(std::to_string(std::get<std::uint16_t>(data)));
			break;
		case KVType::Null:
			w.Write("null");
			break;
		case KVType::BinaryBlob:
		{
			const auto& blob = std::get<BinaryBlob>(data);
			w.Newline();
			w.Writeln("#[");
			w.IndentPush();
			std::size_t count = 0;
			for (std::uint8_t b : blob)
			{
				w.Write(detail::UpperHex(b >> 4));
				w.Write(detail::UpperHex(b & 0xF));
				++count;
				if (count % 32 == 0) w.Newline();
				else w.Write(' ');
			}
			w.IndentPop();
			if (count % 32 != 0) w.Newline();
			w.Write("]");
			break;
		}
		default:
			throw std::runtime_error("KVValue: unknown type in serialize");
		}
	}

	inline void KVObject::WriteKey(IndentedTextWriter& w, const std::string& key)
	{
		
		bool needs_quote = key.empty();
		if (!key.empty() && std::isdigit(static_cast<unsigned char>(key[0])))
			needs_quote = true;

		std::string out;
		out.reserve(key.size() + 2);
		out.push_back('"');

		for (char c : key)
		{
			switch (c)
			{
			case '\t': needs_quote = true; out += "\\t";  break;
			case '\n': needs_quote = true; out += "\\n";  break;
			case '"':  needs_quote = true; out += "\\\""; break;
			case '\\': needs_quote = true; out += "\\\\"; break;
			default:
				if (c != '.' && c != '_' &&
					std::isalnum(static_cast<unsigned char>(c)) == 0)
					needs_quote = true;
				out.push_back(c);
				break;
			}
		}
		out.push_back('"');

		if (needs_quote)
			w.Write(out);
		else
			w.Write(key);

		w.Write(" = ");
	}

	inline void KVObject::SerializeObject(IndentedTextWriter& w) const
	{
		if (key.has_value()) w.Newline();
		w.Writeln("{");
		w.IndentPush();

		for (const auto& [k, v] : items_)
		{
			WriteKey(w, k);
			v.Serialize(w);
			w.Newline();
		}

		w.IndentPop();
		w.Write("}");
	}

	inline void KVObject::SerializeArray(IndentedTextWriter& w) const
	{
		w.Newline();
		w.Writeln("[");
		w.IndentPush();

		for (const auto& [_, v] : items_)
		{
			v.Serialize(w);
			w.Writeln(",");
		}

		w.IndentPop();
		w.Write("]");
	}

	inline void KVObject::Serialize(IndentedTextWriter& w) const
	{
		if (is_array) SerializeArray(w);
		else          SerializeObject(w);
	}

	struct KV3File final
	{
		KV3ID    encoding;
		KV3ID    format;
		KVObject root;

		KV3File(KVObject root, KV3ID encoding, KV3ID format)
			: encoding(std::move(encoding))
			, format(std::move(format))
			, root(std::move(root))
		{
		}

		void WriteText(IndentedTextWriter& w) const
		{
			w.Writeln("<!-- kv3 encoding:" + encoding.ToString()
					  + " format:" + format.ToString() + " -->");
			if (format.name == "vrfunknown")
			{
				w.Writeln("<!-- unknown format -->");
			}
			root.Serialize(w);
		}

		[[nodiscard]] std::string ToString() const
		{
			IndentedTextWriter w;
			WriteText(w);
			return w.Str();
		}
	};

	class KV3ParseError final : public std::runtime_error
	{
		std::size_t byte_{};
	public:
		KV3ParseError(std::size_t byte, std::string msg)
			: std::runtime_error(std::move(msg)), byte_(byte)
		{
		}
		[[nodiscard]] std::size_t Byte() const noexcept
		{
			return byte_;
		}
	};

	namespace detail
	{

		class Lexer final
		{
			std::string_view src_;
			std::size_t      pos_{ 0 };
		public:
			explicit Lexer(std::string_view src) noexcept : src_(src)
			{
			}

			[[nodiscard]] std::size_t  Pos()    const noexcept
			{
				return pos_;
			}
			[[nodiscard]] bool         Eof()    const noexcept
			{
				return pos_ >= src_.size();
			}
			[[nodiscard]] char         Peek(std::size_t ahead = 0) const noexcept
			{
				return (pos_ + ahead < src_.size()) ? src_[pos_ + ahead] : '\0';
			}
			[[nodiscard]] std::string_view PeekN(std::size_t n) const noexcept
			{
				const std::size_t avail = (pos_ < src_.size()) ? src_.size() - pos_ : 0;
				return src_.substr(pos_, std::min(n, avail));
			}

			char Advance() noexcept
			{
				return pos_ < src_.size() ? src_[pos_++] : '\0';
			}
			void Skip(std::size_t n = 1) noexcept
			{
				pos_ = std::min(pos_ + n, src_.size());
			}

			[[nodiscard]] bool StartsWith(std::string_view pattern) const noexcept
			{
				return PeekN(pattern.size()) == pattern;
			}
		};

		[[nodiscard]] inline bool TrailingBackslashEscape(std::string_view s) noexcept
		{
			int n = 0;
			for (std::size_t i = s.size(); i > 0 && s[i - 1] == '\\'; --i) ++n;
			return (n % 2) == 1;
		}

		[[nodiscard]] inline std::string Unescape(std::string_view s)
		{
			std::string r;
			r.reserve(s.size());
			bool esc = false;
			for (char c : s)
			{
				if (c == '\\' && !esc)
				{
					esc = true; continue;
				}
				if (esc)
				{
					switch (c)
					{
					case 'n':  r.push_back('\n'); break;
					case 't':  r.push_back('\t'); break;
					default:   r.push_back(c);    break;
					}
					esc = false;
				}
				else
				{
					r.push_back(c);
				}
			}
			return r;
		}

		[[nodiscard]] inline std::string Unescape(std::string s)
		{
			std::size_t w = 0;
			bool esc = false;
			for (std::size_t i = 0; i < s.size(); ++i)
			{
				char c = s[i];
				if (c == '\\' && !esc)
				{
					esc = true; continue;
				}
				if (esc)
				{
					switch (c)
					{
					case 'n': s[w++] = '\n'; break;
					case 't': s[w++] = '\t'; break;
					default:  s[w++] = c;    break;
					}
					esc = false;
				}
				else
				{
					s[w++] = c;
				}
			}
			s.resize(w);
			return s;
		}

		[[nodiscard]] inline std::pair<std::optional<KV3ID>, std::optional<KV3ID>>
			ParseHeaderIds(std::string_view hdr)
		{
			const auto start = hdr.find("kv3");
			if (start == std::string_view::npos) return { std::nullopt, std::nullopt };
			const std::string_view h = hdr.substr(start);

			constexpr std::string_view kVersion = ":version{";
			auto parse_one = [&] (std::size_t begin_pos) -> std::optional<KV3ID> {
				const auto colon = h.find(kVersion, begin_pos);
				if (colon == std::string_view::npos) return std::nullopt;
				const std::string_view name = h.substr(begin_pos, colon - begin_pos);
				const std::size_t gs = colon + kVersion.size();
				const auto ge = h.find('}', gs);
				if (ge == std::string_view::npos) return std::nullopt;
				const auto g = Guid::TryParse(h.substr(gs, ge - gs));
				if (!g) return std::nullopt;
				return KV3ID{ std::string(name), *g };
			};

			std::optional<KV3ID> enc, fmt;
			constexpr std::string_view kEnc = "encoding:";
			constexpr std::string_view kFmt = "format:";
			if (auto i = h.find(kEnc); i != std::string_view::npos)
				enc = parse_one(i + kEnc.size());
			if (auto i = h.find(kFmt); i != std::string_view::npos)
				fmt = parse_one(i + kFmt.size());
			return { enc, fmt };
		}

		[[nodiscard]] inline BinaryBlob BlobFromHex(std::string_view hex)
		{
			if (hex.size() % 2 != 0)
				return {};
			
			BinaryBlob result;
			result.reserve(hex.size() / 2);
			
			for (std::size_t i = 0; i < hex.size(); i += 2)
			{
				if (auto byte = HexByte(hex, i))
					result.push_back(*byte);
				else
					return {};
			}
			
			return result;
		}

		enum class State : std::uint8_t
		{
			Header,
			SeekValue,
			PropName,
			PropNameQuoted,
			InObject,
			InArray,
			ReadString,
			ReadStringMulti,
			ReadBinaryBlob,
			ReadNumber,
			ReadFlagged,
			LineComment,
			BlockComment,
		};

		class Parser final
		{
			std::string storage_;
			Lexer       L_;

			std::string              cur_name_;
			std::string              cur_buf_;
			std::vector<KVObject>    obj_stack_;
			std::vector<State>       states_;
			std::string              header_;

			[[nodiscard]] State Top() const
			{
				return states_.back();
			}
			void Push(State s)
			{
				states_.push_back(s);
			}
			void Pop()
			{
				states_.pop_back();
			}
			void Replace(State s)
			{
				states_.back() = s;
			}

			void OnHeader(char c)
			{
				cur_buf_.push_back(c);
				const std::size_t n = cur_buf_.size();
				if (n >= 3 && c == '>' &&
					cur_buf_[n - 2] == '-' && cur_buf_[n - 3] == '-')
				{
					header_ = std::move(cur_buf_);
					cur_buf_.clear();
					Replace(State::SeekValue);
				}
			}

			void OnSeekValue(char c)
			{
				if (std::isspace(static_cast<unsigned char>(c)) || c == '=') return;

				if (c == '{')
				{
					Replace(State::InObject);
					obj_stack_.emplace_back(cur_name_, false);
				}
				else if (c == '[')
				{
					Replace(State::InArray);
					obj_stack_.emplace_back(cur_name_, true);
				}
				else if (c == ']')
				{
					Pop();  
					FinishTop(true);
					return;
				}
				else if (c == '"')
				{
					if (L_.StartsWith("\"\"") &&
						(L_.Peek(2) == '\n' || L_.Peek(2) == '\r'))
					{
						L_.Skip(2);
						Replace(State::ReadStringMulti);
					}
					else
					{
						Replace(State::ReadString);
					}
					cur_buf_.clear();
				}
				else if (c == '#' && L_.Peek() == '[')
				{
					L_.Skip(1);
					cur_buf_.clear();
					Replace(State::ReadBinaryBlob);
				}
				else if (L_.Pos() > 0 && 
						 c == 'f' && L_.StartsWith("alse"))
				{
					L_.Skip(4);
					Pop();
					obj_stack_.back().set(cur_name_, KVValue::make_bool(false));
				}
				else if (c == 't' && L_.StartsWith("rue"))
				{
					L_.Skip(3);
					Pop();
					obj_stack_.back().set(cur_name_, KVValue::make_bool(true));
				}
				else if (c == 'n' && L_.StartsWith("ull"))
				{
					L_.Skip(3);
					Pop();
					obj_stack_.back().set(cur_name_, KVValue::make_null());
				}
				else if (std::isdigit(static_cast<unsigned char>(c)) ||
						 (c == '-' && std::isdigit(static_cast<unsigned char>(L_.Peek()))))
				{
					cur_buf_.clear();
					cur_buf_.push_back(c);
					Replace(State::ReadNumber);
				}
				else
				{
					cur_buf_.clear();
					cur_buf_.push_back(c);
					Replace(State::ReadFlagged);
				}
			}

			void OnPropName(char c)
			{
				if (std::isspace(static_cast<unsigned char>(c)))
				{
					cur_name_ = std::move(cur_buf_);
					Replace(State::SeekValue);
				}
				else
				{
					cur_buf_.push_back(c);
				}
			}

			void OnPropNameQuoted(char c)
			{
				if (c == '"' && !TrailingBackslashEscape(cur_buf_))
				{
					cur_name_ = Unescape(std::move(cur_buf_));
					Replace(State::SeekValue);
				}
				else
				{
					cur_buf_.push_back(c);
				}
			}

			void FinishTop(bool as_array)
			{
				KVObject obj = std::move(obj_stack_.back());
				obj_stack_.pop_back();
				const std::string prop_key = obj.key.value_or("");
				obj_stack_.back().set(prop_key, KVValue::make_object(std::move(obj), as_array));
			}

			void OnInObject(char c)
			{
				if (std::isspace(static_cast<unsigned char>(c))) return;
				if (c == '/')
				{
					cur_buf_.clear();
					cur_buf_.push_back(c);
					Push(State::LineComment);
					return;
				}
				if (c == '}')
				{
					Pop();
					FinishTop(false);
					return;
				}
				cur_buf_.clear();
				if (c == '"') Push(State::PropNameQuoted);
				else
				{
					Push(State::PropName); cur_buf_.push_back(c);
				}
			}

			void OnInArray(char c)
			{
				if (c == ']')
				{
					Pop();
					FinishTop(true);
					return;
				}
				if (std::isspace(static_cast<unsigned char>(c)) || c == ',')
				{
					Push(State::SeekValue);
					return;
				}
				throw KV3ParseError(L_.Pos(), std::string("Unexpected char in array: '") + c + "'");
			}

			void OnReadString(char c)
			{
				if (c == '"' && !TrailingBackslashEscape(cur_buf_))
				{
					Pop();
					obj_stack_.back().set(cur_name_, KVValue::make_string(Unescape(std::move(cur_buf_))));
				}
				else
				{
					cur_buf_.push_back(c);
				}
			}

			void OnReadStringMulti(char c)
			{
				if (c == '"' && L_.StartsWith("\"\"") && !TrailingBackslashEscape(cur_buf_))
				{
					L_.Skip(2);
					
					std::string s = std::move(cur_buf_);
					std::size_t lo = 0, hi = s.size();
					if (!s.empty() && s[0] == '\n')       lo = 1;
					else if (s.size() >= 2 && s[0] == '\r' && s[1] == '\n') lo = 2;
					if (hi > lo && s[hi - 1] == '\n')
					{
						if (hi - lo >= 2 && s[hi - 2] == '\r') hi -= 2;
						else --hi;
					}
					s.resize(hi);          
					s.erase(0, lo);        
					Pop();
					obj_stack_.back().set(cur_name_, KVValue::make_string(std::move(s)));
				}
				else
				{
					cur_buf_.push_back(c);
				}
			}

			void OnReadBinaryBlob(char c)
			{
				if (c == ']')
				{
					Pop();
					obj_stack_.back().set(cur_name_, KVValue::make_blob(BlobFromHex(cur_buf_)));
				}
				else if (!std::isspace(static_cast<unsigned char>(c)))
				{
					cur_buf_.push_back(c);
				}
			}

			void OnReadNumber(char c)
			{
				if (std::isspace(static_cast<unsigned char>(c)) || c == ',')
				{
					Pop();
					if (cur_buf_.find('.') != std::string::npos)
					{
						double v;
						auto [ptr, ec] = std::from_chars(cur_buf_.data(),
														 cur_buf_.data() + cur_buf_.size(),
														 v);
						if (ec == std::errc())
							obj_stack_.back().set(cur_name_, KVValue::make_double(v));
					}
					else
					{
						std::int64_t v;
						auto [ptr, ec] = std::from_chars(cur_buf_.data(),
														 cur_buf_.data() + cur_buf_.size(),
														 v);
						if (ec == std::errc())
							obj_stack_.back().set(cur_name_, KVValue::make_int64(v));
					}
					cur_buf_.clear();
				}
				else
				{
					cur_buf_.push_back(c);
				}
			}

			void OnReadFlagged(char c)
			{
				if (!std::isspace(static_cast<unsigned char>(c)))
				{
					cur_buf_.push_back(c);
					return;
				}
				Pop();
				std::string s = std::move(cur_buf_);
				const auto colon = s.find(':');
				if (colon == std::string::npos)
					throw KV3ParseError(L_.Pos(), "Invalid flagged value: " + s);

				const std::string_view flag_str(s.data(), colon);
				const std::string_view rest(s.data() + colon + 1, s.size() - colon - 1);

				KVFlag flag{};
				if (flag_str == "resource")           flag = KVFlag::Resource;
				else if (flag_str == "resource_name") flag = KVFlag::ResourceName;
				else if (flag_str == "panorama")      flag = KVFlag::Panorama;
				else if (flag_str == "soundevent")    flag = KVFlag::SoundEvent;
				else if (flag_str == "subclass")      flag = KVFlag::SubClass;
				else if (flag_str == "entity_name")   flag = KVFlag::EntityName;
				else throw KV3ParseError(L_.Pos(), "Unknown flag: " + std::string(flag_str));

				const bool in_arr = !states_.empty() && states_.back() == State::InArray;
				const std::size_t trim = in_arr ? 2u : 1u;
				if (rest.size() < 1 + trim)
					throw KV3ParseError(L_.Pos(), "Invalid flagged value body");
				
				s.erase(0, colon + 2);                          
				s.resize(s.size() - trim);                      
				obj_stack_.back().set(cur_name_, KVValue::make_string(std::move(s), flag));
			}

			void OnLineComment(char c)
			{
				if (cur_buf_.size() == 1 && c == '*')
				{
					Replace(State::BlockComment);
				}
				if (c == '\n') Pop();
				else if (c != '\r') cur_buf_.push_back(c);
			}

			void OnBlockComment(char c)
			{
				if (c == '/' && !cur_buf_.empty() && cur_buf_.back() == '*') Pop();
				cur_buf_.push_back(c);
			}

			void Dispatch(char c)
			{
				switch (Top())
				{
				case State::Header:          OnHeader(c);           break;
				case State::PropName:        OnPropName(c);        break;
				case State::PropNameQuoted:  OnPropNameQuoted(c); break;
				case State::SeekValue:       OnSeekValue(c);       break;
				case State::InObject:        OnInObject(c);        break;
				case State::InArray:         OnInArray(c);         break;
				case State::ReadString:      OnReadString(c);      break;
				case State::ReadStringMulti: OnReadStringMulti(c); break;
				case State::ReadBinaryBlob:  OnReadBinaryBlob(c); break;
				case State::ReadNumber:      OnReadNumber(c);      break;
				case State::ReadFlagged:     OnReadFlagged(c);     break;
				case State::LineComment:     OnLineComment(c);     break;
				case State::BlockComment:    OnBlockComment(c);    break;
				}
			}

		public:
			explicit Parser(std::string src)
				: storage_(std::move(src)), L_(storage_)
			{
				states_.push_back(State::Header);
				obj_stack_.emplace_back(std::string(""), false);
			}

			[[nodiscard]] KV3File Parse()
			{
				while (!L_.Eof())
				{
					char c = L_.Advance();
					if (states_.empty())
					{
						if (c != '\0' && std::isspace(static_cast<unsigned char>(c)) == 0)
							throw KV3ParseError(L_.Pos(), "Unexpected content after end of document");
						continue;
					}
					Dispatch(c);
					if (c == '\0') break;
				}

				auto [enc_opt, fmt_opt] = ParseHeaderIds(header_);
				KV3ID enc = enc_opt.value_or(KV3IDLookup::Get("text"));
				KV3ID fmt = fmt_opt.value_or(KV3IDLookup::Get("generic"));

				const KVObject& top_obj = obj_stack_.front();
				const KVValue* v = top_obj.try_get("");
				if (!v || (!v->is_object() && !v->is_array()))
					throw std::runtime_error("KV3: could not locate root object");

				KVObject root = *std::get<std::shared_ptr<KVObject>>(v->data);
				return KV3File(std::move(root), std::move(enc), std::move(fmt));
			}
		};

		[[nodiscard]] inline KV3File ParseString(std::string src)
		{
			return Parser(std::move(src)).Parse();
		}

		[[nodiscard]] inline KV3File ParseStream(std::istream& is)
		{
			std::string data((std::istreambuf_iterator<char>(is)),
							 std::istreambuf_iterator<char>());
			return ParseString(std::move(data));
		}

		[[nodiscard]] inline KV3File ParseFile(const std::string& path)
		{
			std::ifstream f(path, std::ios::binary);
			if (!f) throw std::runtime_error("KV3: cannot open file: " + path);
			return ParseStream(f);
		}

	} 

	class KV3Reader final
	{
	public:
		[[nodiscard]] static KV3File Parse(const std::string& path)
		{
			return detail::ParseFile(path);
		}
		[[nodiscard]] static KV3File Parse(std::istream& is)
		{
			return detail::ParseStream(is);
		}
		[[nodiscard]] static KV3File ParseText(std::string src)
		{
			return detail::ParseString(std::move(src));
		}
	};

	[[nodiscard]] inline KVObject::Items::const_iterator begin(const KVValue& v)
	{
		static const KVObject empty(std::nullopt, true, 0);
		if (!v.is_object() && !v.is_array()) return empty.end();
		return v.as_object().begin();
	}
	[[nodiscard]] inline KVObject::Items::const_iterator end(const KVValue& v)
	{
		static const KVObject empty(std::nullopt, true, 0);
		if (!v.is_object() && !v.is_array()) return empty.end();
		return v.as_object().end();
	}

} 

#endif __KEYVALUE_3_H__
