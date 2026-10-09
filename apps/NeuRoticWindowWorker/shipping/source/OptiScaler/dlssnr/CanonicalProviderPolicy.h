// Generated from the byte-pinned canonical manifest; identity, not lifetime authority.
#pragma once
#include <cstdint>
namespace DlssNr::Canonical {
struct Member { const wchar_t* name; std::uint64_t bytes; const char* sha256; };
inline constexpr Member Archive{L"streamline (Good).zip", 143760859ull, "f1898231b171267649916e866043fe3ffded5fc7a1b801bfad1dc57a3723cffa"};
inline constexpr Member Members[] = {
    {L"nis.license.txt", 1199ull, "f80ecfbce8a84a5b4c1c59dc5a9f0ee9cf5a989c9fba3a9486ee874f7595a454"},
    {L"nvngx_dlss.dll", 58956400ull, "c85f971ce023c9f3492fc7455f0b01a24ba18ea39636407a846902c4360b0b7e"},
    {L"nvngx_dlss.license.txt", 27316ull, "b6f4b4b6f582c9523ed4dfe89a4fae4cfc9c61f9c57e335e00ccee5f9b2b2e4b"},
    {L"nvngx_dlssg.dll", 7453808ull, "5d5cbf14d2727d47f93fd10bf77bd91708ae122482a6f86fd564971641ebd47b"},
    {L"nvngx_dlssnr.dll", 165840496ull, "ceb6432f6fbdf44d886014bcd47241932bf8b67439feef9bbdd0961436662650"},
    {L"reflex.license.txt", 20504ull, "ebf83c07fb3b2939908c3795d887afde3161c89a28ba391724efc784ce1bdabe"},
    {L"sl.common.dll", 830592ull, "a4b2b5acbe49fbc6d44dd432cac19cd53218f698b2539dc7ed0fb268c72cfc8d"},
    {L"sl.dlss.dll", 421504ull, "1eb5fb3d6f01d340fe086d981cc2de4f18aa6d05ee276e5cf28ecd54818dcc8b"},
    {L"sl.dlss_g.dll", 625792ull, "b8b5effd7debdb750abd216de43385fb653261712bc315d85eba68811fb3ee02"},
    {L"sl.dlss_nr.dll", 401024ull, "9f6672e5e0170dc118a3188d21bda187e1fc1aa3502895b21ab846d23165c11d"},
    {L"sl.interposer.dll", 651392ull, "27b2190057994c0b287c2c5716953bf1586f6499ac12fbbb2092b9aaf8396570"},
    {L"sl.nis.dll", 1155200ull, "6039e38a1af56c8e86f3e936596e2db910bf3d76bbf4268562a3b13763049dfa"},
    {L"sl.pcl.dll", 360064ull, "12aa4e76c28a27c735e4ecb3072f44d09428acb107b70ac38e4bd48ddb05f88d"},
    {L"sl.reflex.dll", 382080ull, "ecf12973cdcec2ffced2ea77b1c7e45f4d387e7c864ddb5531b66a6f947effb3"},
};
}
