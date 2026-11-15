#include "png_time_alignment.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <set>
using hro::pi::PngTimeAlignment;
void check(bool ok) {if(!ok)throw std::runtime_error("PNG alignment test failed");}
int main() {
    constexpr std::int64_t start=1794484800000LL; // A positive UTC 20-minute boundary.
    PngTimeAlignment aligned;
    std::set<std::int64_t> seconds;
    // Reproduce alternating fast/slow completion timestamps around second boundaries.
    const int jitter[]={10,-34,-62,14,-30,-75,17,-20,-71,21,-25,-68};
    std::set<std::int64_t> raw;
    for(std::uint64_t n=0;n<3601;++n) {
        const auto timestamp=start+n*1000+jitter[n%12];
        raw.insert(timestamp/1000);
        auto p=aligned.place(timestamp,n+426536);check(bool(p));
        check(p->second==start/1000+static_cast<std::int64_t>(n) && !p->reanchored);
        check(seconds.insert(p->second).second);
    }
    check(raw.size()<seconds.size()); // Raw flooring would generate false missing seconds.
    // Genuine missing sequences retain one empty second per lost packet.
    PngTimeAlignment loss;
    check(loss.place(start,100)->second==start/1000);
    check(loss.place(start+3000,103)->second==start/1000+3);
    check(!loss.place(start+3000,103)); // Duplicate does not overwrite.
    check(!loss.place(start+2000,102)); // Late/out-of-order record does not move backward.
    check(loss.place(start+4000,104)->second==start/1000+4);
    check(loss.place(start+8000,0)->reanchored); // Engine restarted; sequence counter reset.
    check(loss.place(start+9000,1)->second==start/1000+9);
    PngTimeAlignment pause;
    check(bool(pause.place(start,0)));
    auto p=pause.place(start+11000,1);check(p && p->reanchored && p->second==start/1000+11);
    check(!pause.place(start-60000,2)); // Large backward clock change must not overwrite old blocks.
    check(!pause.place(0,3));
    // Corrected placement determines rollover; raw milliseconds remain untouched by the API.
    PngTimeAlignment boundary;
    const auto stamp=start+1199700;
    p=boundary.place(stamp,0);check(p && p->second==start/1000+1200);
    check(boundary.place(start+1200656,1)->second==start/1000+1201);
    // Uint64 sequence overflow / absurd leaps cannot overflow signed timestamp arithmetic.
    PngTimeAlignment huge;check(bool(huge.place(start,0)));
    p=huge.place(start+1000,UINT64_MAX);check(!p);
    std::cout<<"PNG alignment: jitter, missing sequences, duplicates, restart, clock changes and rollover passed\n";
}
