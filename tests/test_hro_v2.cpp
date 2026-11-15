#include "hro_header.h"
#include "hro_writer.h"
#include "hourly_hro.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <unistd.h>
namespace fs = std::filesystem;
void check(bool value) { if (!value) throw std::runtime_error("HRO v2 test failed"); }
std::array<char, hro::HEADER_SIZE> headerOf(const fs::path& p) {
    std::array<char,hro::HEADER_SIZE> b{}; std::ifstream f(p,std::ios::binary);
    f.read(b.data(),b.size()); check(bool(f)); return b;
}
unsigned char byteAt(const fs::path& p, std::size_t pos) {
    std::ifstream f(p,std::ios::binary); f.seekg(pos); char c; f.read(&c,1); check(bool(f)); return c;
}
int main() {
    char pattern[] = "/tmp/hro-v2-tests-XXXXXX"; const auto dir = fs::path(mkdtemp(pattern));
    try {
        hro::HroMetadata m; m.fftCenterHz = 900; m.observer = "日本語 \"test\" }";
        m.fileStartUtc = "2026-10-04T00:00:00Z"; m.fileHourLocal = "2026-10-04T09:00:00";
        m.timeZone = "Asia/Tokyo"; m.startTimeSource = "SYSTEM";
        std::array<char,hro::HEADER_SIZE> b{}; check(hro::HroHeader::encode(m,b));
        hro::HroHeader h; check(h.load(b) && h.isValidV2()); std::string value;
        check(h.getValue("FFT_MIN_HZ",value) && value=="600.0");
        check(h.getValue("FFT_MAX_HZ",value) && value=="1200.0");
        auto end = std::find(b.begin(),b.end(),0); check(end != b.end());
        check(std::all_of(end,b.end(),[](char c){return !c;}));
        auto bad=b; bad.fill(' '); check(!h.load(bad));
        bad=b; bad[std::distance(b.begin(),end)+1]='x'; check(!h.load(bad));
        bad=b; bad[0]='['; check(!h.load(bad));
        const std::string body(b.begin(),end);
        const auto malformed = [&](std::string text) { std::array<char,hro::HEADER_SIZE> data{}; std::copy(text.begin(),text.end(),data.begin()); check(!h.load(data)); };
        malformed(body+"{}"); malformed(body.substr(0,body.size()-1));
        malformed("{\"HRO_FILE_VERSION\":2,"+body.substr(1));
        auto wrong=body; auto at=wrong.find("\"FFT_BIN_COUNT\": 601"); wrong.replace(at,20,"\"FFT_BIN_COUNT\": 501"); malformed(wrong);
        m.observer.assign(8192,'x'); check(!hro::HroHeader::encode(m,b)); m.observer="test";
        m.fftCenterHz=200; check(!hro::HroHeader::encode(m,b)); m.fftCenterHz=900;
        const auto file=dir/"writer.hro";
        std::array<float,hro::BIN_COUNT> fft{}; fft.fill(-12.5f); fft[0]=1.0f;
        { hro::HroWriter w; check(w.open(file.string())); check(w.writeHeader(m));
          check(w.writeSpectrum(0,fft.data())); check(w.writeSpectrum(3599,fft.data()));
          check(w.setEvent(7,hro::EVENT_VISUAL)); check(!w.setEvent(7,0x80)); check(!w.writeSpectrum(3600,fft.data()));
          fft[3]=std::numeric_limits<float>::quiet_NaN(); check(!w.writeSpectrum(2,fft.data())); fft[3]=-12.5f;
          check(w.flush()); }
        check(fs::file_size(file.string()+".tmp")==hro::FILE_SIZE);
        { hro::HroWriter w; check(w.resume(file.string()+".tmp")); check(w.hasSpectrum(0)&&!w.hasSpectrum(1)); check(w.close()); }
        check(byteAt(file,hro::VALIDITY_MAP_OFFSET)==1);
        check(byteAt(file,hro::VALIDITY_MAP_OFFSET+449)==0x80);
        check(byteAt(file,hro::EVENT_MAP_OFFSET+7)==1);
        const auto pos=hro::spectrumOffset(0); check(byteAt(file,pos)==0 && byteAt(file,pos+2)==0x80 && byteAt(file,pos+3)==0x3f);
        { hro::HroWriter w; check(!w.open(file.string())); }
        HroConfig c; c.setDefaults(); c.fft_center_hz=900; c.screenshot_prefix="TEST";
        constexpr std::int64_t hour=1791072000; // 2026-10-04 00:00 UTC
        const auto records=dir/"records";
        const auto first=records/"2026"/"202610"/"20261004"/"TEST202610040900.hro";
        { hro::pi::HourlyHroRecorder r(records,c);
          check(r.record((hour+5)*1000+123, 0, fft));
          fft[0]=2.0f; check(r.record((hour+5)*1000+124, 1, fft));
          check(r.record((hour+4)*1000, 2, fft)); check(r.stop()); }
        check(fs::file_size(first)==8192+3*2420);
        check(h.load(headerOf(first)));
        check(h.getValue("RECORD_LAYOUT",value) && value=="SEQUENTIAL_FFT_V1");
        // Byte-level checks: same-second FFTs and backward time preserve sequence.
        check(byteAt(first,8192+8)==0);
        check(byteAt(first,8192+2420+8)==1);
        check(byteAt(first,8192+4840+8)==2);
        check(byteAt(first,8192+16+2)==0x80); // original 1.0
        check(byteAt(first,8192+2420+16+3)==0x40); // next 2.0
        { hro::pi::HourlyHroRecorder r(records,c);
          check(r.record((hour+6)*1000, 0, fft));
          check(r.record((hour+3600)*1000, 1, fft));
          check(r.record((hour+7)*1000, 2, fft)); check(r.stop()); }
        const auto day=first.parent_path();
        check(fs::file_size(day/"TEST202610040900.part1.hro")==8192+2420);
        check(fs::file_size(day/"TEST202610040900.part2.hro")==8192+2420);
        check(fs::file_size(day/"TEST202610041000.hro")==8192+2420);
        // Existing legacy files remain untouched; torn temporary files are preserved.
        const auto torn=day/"TEST202610040900.part3.hro.tmp";
        { std::ofstream out(torn); out << "incomplete"; }
        { hro::pi::HourlyHroRecorder r(records,c);
          check(r.record((hour+8)*1000, 0, fft)); check(r.stop()); }
        check(fs::file_size(torn)==10);
        check(fs::exists(day/"TEST202610040900.part4.hro"));
        // No one-second deduplication or 3600-record capacity limit.
        const auto many=dir/"many";
        { hro::pi::HourlyHroRecorder r(many,c);
          for (std::uint64_t n=0; n<3601; ++n)
              check(r.record(hour*1000, n, fft));
          check(r.stop()); }
        check(fs::file_size(many/"2026"/"202610"/"20261004"/"TEST202610040900.hro")==8192+3601*2420);
        const auto blocked=dir/"blocked";
        { std::ofstream out(blocked); out << "not a directory"; }
        { hro::pi::HourlyHroRecorder r(blocked,c); check(!r.record(hour*1000,0,fft)); }
        fs::remove_all(dir); std::cout<<"HRO legacy and sequential records, restart and clock reversal passed\n";
    } catch (...) { fs::remove_all(dir); throw; }
}
