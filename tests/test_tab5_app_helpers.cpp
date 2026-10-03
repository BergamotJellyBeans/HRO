#include "tab5_helpers.hpp"
#include "hro_sdr_config.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
using namespace hro::tab5::app;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"Failed line %d: %s\n",__LINE__,#x); return 1; } } while(0)
int main() {
 char out[64];
 CHECK(get_form_value("other_name=bad&name=A%2BB+Tokyo&x=1","name",out,sizeof out));
 CHECK(std::strcmp(out,"A+B Tokyo")==0);
 CHECK(!get_form_value("other_name=bad","name",out,sizeof out));
 url_decode(out,4,"abcdef"); CHECK(std::strcmp(out,"abc")==0);
 url_decode(out,sizeof out,"%ZZ%20x"); CHECK(std::strcmp(out,"%ZZ x")==0);
 CHECK(point_in_rect(10,20,10,20,30,40)); CHECK(!point_in_rect(40,20,10,20,30,40));
 CHECK(format_png_filename("HRO",0,out,sizeof out)); CHECK(std::strcmp(out,"HRO197001010900.png")==0);
 CHECK(format_png_filename("HRO",54000,out,sizeof out)); CHECK(std::strcmp(out,"HRO197001020000.png")==0);
 CHECK(!format_png_filename("../x",0,out,sizeof out)); CHECK(!format_png_filename("HRO",0,out,4));
 CHECK(history_physical_index(0,1200,1199,1200)==1199);
 CHECK(history_physical_index(1,1200,1199,1200)==0);
 CHECK(history_physical_index(2,3,3,1200)==2);
 CHECK(history_physical_index(3,3,3,1200)==1200);
 Tab5Config c{}; c.frequency_hz=53372000; c.fft_center_hz=780; c.fft_range_hz=300; c.audio_volume=50; std::strcpy(c.screenshot_prefix,"HRO");
 CHECK(validate_hro_config(c,out,sizeof out));
 for (int gain : hro::SDR_GAIN_VALUES) {
   c.sdr_gain = gain;
   CHECK(validate_hro_config(c,out,sizeof out) == (gain != 480));
 }
 for (int gain : {-1, 10, 500}) {
   c.sdr_gain = gain; CHECK(!validate_hro_config(c,out,sizeof out));
 }
 c.sdr_gain = hro::DEFAULT_SDR_GAIN;
 for (const char* address : {"192.168.0.32", "10.0.0.1", ""}) {
   std::strcpy(c.pi5_address,address); CHECK(validate_hro_config(c,out,sizeof out));
 }
 for (const char* address : {"256.1.2.3", "224.0.0.1", "1.2.3", "1.2.3.4x", "-1.2.3.4"}) {
   std::strcpy(c.pi5_address,address); CHECK(!validate_hro_config(c,out,sizeof out));
 }
 c.pi5_address[0] = '\0';
 CHECK(validate_hro_config(c,out,sizeof out));
 auto t=calculate_tuning(c); CHECK(t.actual_lo_hz==53436000); CHECK(t.actual_if_hz==-64000); CHECK(t.nco_shift_hz==780);
 c.frequency_hz+=123; t=calculate_tuning(c); CHECK(t.nco_shift_hz==657);
 c.fft_range_hz=600; CHECK(!validate_hro_config(c,out,sizeof out));
 c.fft_range_hz=300; c.level_average_range_hz=301; CHECK(!validate_hro_config(c,out,sizeof out));
 c.level_average_range_hz=0; c.latitude=91; CHECK(!validate_hro_config(c,out,sizeof out));
 return 0;
}
