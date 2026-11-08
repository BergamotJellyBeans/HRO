#include "archive_hro.hpp"
#include "hro_header.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <iostream>
#include <algorithm>
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
        fs::remove_all(root);std::cout<<"Archive: sequential/legacy, partial tail, pagination, uint64 and path protection passed\n";
    } catch (...) {fs::remove_all(root);throw;}
}
