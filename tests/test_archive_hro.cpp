#include "archive_hro.hpp"
#include "hro_header.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <unistd.h>
namespace fs = std::filesystem;
using Json = nlohmann::json;
void check(bool value) { if (!value) throw std::runtime_error("Archive HRO test failed"); }
void header(const fs::path& path, bool sequential) {
    hro::HroMetadata m; m.fftCenterHz=780;
    m.fileStartUtc="2026-11-07T13:00:00Z"; m.fileHourLocal="2026-11-07T22:00:00";
    m.timeZone="Asia/Tokyo"; m.startTimeSource="SYSTEM";
    std::array<char,hro::HEADER_SIZE> bytes{}; check(hro::HroHeader::encode(m,bytes));
    auto j=Json::parse(bytes.data());
    if (sequential) {
        j["RECORD_LAYOUT"]="SEQUENTIAL_FFT_V1"; j["RECORD_SIZE"]=2420; j["DATA_OFFSET"]=8192;
        j["RECORD_FIELDS"]={"TIMESTAMP_MS_INT64","SEQUENCE_UINT64","FFT_FLOAT32"};
        for (const auto* k : {"RECORD_SLOTS","RECORD_INTERVAL_SEC","VALIDITY_MAP_SIZE","EVENT_MAP_SIZE"}) j[k]=0;
    }
    auto text=j.dump(); bytes.fill(0); std::copy(text.begin(),text.end(),bytes.begin());
    std::ofstream out(path,std::ios::binary); out.write(bytes.data(),bytes.size());
}
void append(const fs::path& path, std::uint64_t sequence) {
    std::array<unsigned char,2420> bytes{};
    const auto put=[&](unsigned at,std::uint64_t n) {for(unsigned i=0;i<8;++i)bytes[at+i]=n>>(8*i);};
    put(0,1794056400123ULL); put(8,sequence);
    std::ofstream out(path,std::ios::binary|std::ios::app);
    out.write(reinterpret_cast<char*>(bytes.data()),bytes.size());
}
int main() {
    char name[]="/tmp/hro-archive-XXXXXX"; const fs::path root=mkdtemp(name);
    try {
        const auto sequential=root/"test.hro.tmp";
        header(sequential,true); append(sequential,UINT64_MAX-1); append(sequential,UINT64_MAX);
        {std::ofstream out(sequential,std::ios::app);out<<"torn";}
        auto page=hro::archive::page(root,"test.hro.tmp",0,1);
        check(page["record_count"]==2 && page["trailing_bytes"]==4 && page["has_more"]==true);
        check(page["records"][0]["sequence"]=="18446744073709551614");
        check(page["records"][0]["timestamp_ms"]==1794056400123ULL);
        check(page["records"][0]["valid"]==true && page["records"][0]["fft_db"].size()==601);
        page=hro::archive::page(root,"test.hro.tmp",1);
        check(page["records"][0]["sequence"]=="18446744073709551615" && page["has_more"]==false);
        check(hro::archive::page(root,"test.hro.tmp",2)["records"].empty());
        bool rejected=false;try{hro::archive::page(root,"test.hro.tmp",3);}catch(...){rejected=true;} check(rejected);
        rejected=false;try{hro::archive::page(root,"test.hro.tmp",0,1201);}catch(...){rejected=true;} check(rejected);
        const auto legacy=root/"old.hro"; header(legacy,false); fs::resize_file(legacy,hro::FILE_SIZE);
        {std::fstream out(legacy,std::ios::binary|std::ios::in|std::ios::out);out.seekp(hro::VALIDITY_MAP_OFFSET);out.put(1);}
        page=hro::archive::page(root,"old.hro",0,2);
        check(page["records"][0]["valid"]==true && page["records"][1]["valid"]==false);
        check(page["records"][0]["sequence"].is_null());
        fs::resize_file(legacy,hro::DATA_OFFSET+50);
        page=hro::archive::page(root,"old.hro",0,1);
        check(page["trailing_bytes"]!=0 && page["records"][0]["valid"]==false);
        {std::ofstream out(root/"broken.hro");out<<"broken";}
        {std::ofstream out(root/"._ignore.hro");}
        fs::create_symlink("/etc/passwd",root/"outside.hro");
        rejected=false;try{hro::archive::page(root,"outside.hro",0);}catch(...){rejected=true;}check(rejected);
        const auto listing=hro::archive::list(root);check(listing.size()==4);
        check(std::any_of(listing.begin(),listing.end(),[](const Json& item){return item["name"]=="broken.hro" && item.contains("error");}));
        {std::ofstream out(root/"partial.png");}
        {std::ofstream out(root/"partial.json");out<<R"({"data_quality":{"expected_seconds":1200,"received_seconds":537}})";}
        {std::ofstream out(root/"legacy.png");}
        {std::ofstream out(root/"bad.png");}
        {std::ofstream out(root/"bad.json");out<<"broken";}
        auto images=hro::archive::listImages(root);check(images.size()==3);
        check(images[0]["quality"]=="metadata_error");
        check(images[1]["quality"]=="unknown");
        check(images[2]["quality"]=="partial" && images[2]["received_seconds"]==537);
        const auto dated=root/"dated";
        const auto makeDay=[&](const std::string& day, bool image, bool hro) {
            const auto dir=dated/day.substr(0,4)/day.substr(0,6)/day;
            fs::create_directories(dir);
            if(image) {std::ofstream out(dir/("BJ"+day+"0000.png"));}
            if(hro) header(dir/("BJ"+day+"0000.hro.tmp"),true);
        };
        makeDay("20261009",true,true); makeDay("20261010",true,true);
        makeDay("20261108",true,true); makeDay("20261109",true,true);
        auto window=hro::archive::listWindow(dated,true,"20261109");
        check(window["start_date"]=="2026-10-10" && window["end_date"]=="2026-11-08");
        check(window["items"].size()==2 && window["has_more"]==true && window["next_before"]=="20261010");
        auto older=hro::archive::listWindow(dated,true,window["next_before"]);
        check(older["items"].size()==1 && older["has_more"]==false);
        check(older["items"][0]["png_file"]=="BJ202610090000.png");
        window=hro::archive::listWindow(dated,false,"20261109");
        check(window["items"].size()==2 && window["items"][0]["file"]=="2026/202610/20261010/BJ202610100000.hro.tmp");
        check(window["items"][0]["record_count"]==0); // Header-only temporary observation remains visible.
        makeDay("20240101",true,false);
        window=hro::archive::listWindow(dated,true,"20261009");
        check(window["items"].empty() && window["has_more"]==true); // Empty period must not terminate navigation.
        window=hro::archive::listWindow(dated,true,"20240301");
        check(window["start_date"]=="2024-01-31" && window["end_date"]=="2024-02-29");
        makeDay("20231231",false,true);
        window=hro::archive::listWindow(dated,false,"20240102");
        check(window["start_date"]=="2023-12-03" && window["items"].size()==1);
        rejected=false;try{hro::archive::listWindow(dated,true,"20260230");}catch(...){rejected=true;}check(rejected);
        rejected=false;try{hro::archive::listWindow(dated,true,"../../etc");}catch(...){rejected=true;}check(rejected);
        check(hro::archive::listWindow(root/"absent",true,"20261109")["items"].empty());
        // Compact PNG view: only matching hours; partial tails and gaps stay missing.
        const auto fast=root/"fast", day=fast/"2026/202611/20261107";
        fs::create_directories(day);
        const auto recording=day/"BJ202611072200.hro.tmp";
        header(recording,true);
        const std::int64_t start=1794056400000LL;
        auto writeRecord=[&](const fs::path& file,std::int64_t ms,std::uint64_t seq,float db) {
            std::array<unsigned char,2420> bytes{};
            for (unsigned i=0;i<8;++i) {bytes[i]=std::uint64_t(ms)>>(8*i);bytes[i+8]=seq>>(8*i);}
            std::uint32_t bits;std::memcpy(&bits,&db,4);
            for (unsigned b=0;b<601;++b) for(unsigned i=0;i<4;++i) bytes[16+b*4+i]=bits>>(8*i);
            std::ofstream out(file,std::ios::binary|std::ios::app);out.write(reinterpret_cast<char*>(bytes.data()),bytes.size());
        };
        for (unsigned i=0;i<3600;++i) if(i!=17) writeRecord(recording,start+i*1000+123,100+i,-12.5f);
        {std::ofstream out(recording,std::ios::binary|std::ios::app);out<<"torn";}
        auto begin=std::chrono::steady_clock::now();
        auto frame=hro::archive::imageFrame(fast,"BJ202611072200.png");
        const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-begin).count();
        check(frame.size()==16+1200+1200*601*4 && frame.substr(0,7)=="HROPNG1");
        const auto available=[&](const std::string& bytes) {
            std::uint32_t n=0;for(unsigned i=0;i<4;++i)n|=std::uint32_t(static_cast<unsigned char>(bytes[8+i]))<<(8*i);return n;
        };
        check(available(frame)==1199 && frame[16+17]==0 && frame[16+18]==1);
        std::uint32_t bits=0;for(unsigned i=0;i<4;++i)bits|=std::uint32_t(static_cast<unsigned char>(frame[1216+i]))<<(8*i);
        float db;std::memcpy(&db,&bits,4);check(db==-12.5f);
        frame=hro::archive::imageFrame(fast,"BJ202611072220.png");check(available(frame)==1200);
        fs::rename(recording,day/"BJ202611072200.hro");
        frame=hro::archive::imageFrame(fast,"BJ202611072240.png");check(available(frame)==1200);
        const auto part=day/"BJ202611072300.part1.hro.tmp";header(part,true);
        writeRecord(part,start+3600000+123,0,-7.f);
        frame=hro::archive::imageFrame(fast,"BJ202611072300.png");check(available(frame)==1);
        // Midnight: previous hour's last record rounds into the new date.
        const auto previous=day/"BJ202611072300.part2.hro";header(previous,true);
        writeRecord(previous,start+7200000-233,5,-8.f);
        frame=hro::archive::imageFrame(fast,"BJ202611080000.png");check(available(frame)==1 && frame[16]==1);
        frame=hro::archive::imageFrame(fast,"BJ202611082200.png");check(available(frame)==0);
        for(const auto* bad:{"../BJ202611072200.png","BJ202602302200.png","BJ202611072210.png","BJ202611072400.png"}) {
            rejected=false;try{hro::archive::imageFrame(fast,bad);}catch(const std::invalid_argument&){rejected=true;}check(rejected);
        }
        const auto legacyFrame=day/"LEG202611072200.hro";
        header(legacyFrame,false);fs::resize_file(legacyFrame,hro::FILE_SIZE);
        {std::fstream out(legacyFrame,std::ios::binary|std::ios::in|std::ios::out);out.seekp(hro::VALIDITY_MAP_OFFSET);out.put(1);}
        frame=hro::archive::imageFrame(fast,"LEG202611072200.png");check(available(frame)>0);
        std::cout<<"Compact PNG frame: "<<frame.size()<<" bytes; local initial frame "<<elapsed<<" ms; gap, temporary/final, restart, midnight, legacy and validation passed\n";
        fs::remove_all(root);std::cout<<"Archive: sequential/legacy, partial tail, pagination, uint64 and path protection passed\n";
    } catch (...) {fs::remove_all(root);throw;}
}
