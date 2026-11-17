// Compile with -O3 -std=c++17 -DHEADER="/absolute/path/to/GaleDSP.h". CPU milliseconds, not DAW load.
#include HEADER
#include <xmmintrin.h>
#include <ctime>
#include <iostream>
int main(){
 _mm_setcsr(_mm_getcsr()|0x8040);
 for(int env=0;env<4;env++) for(int on=0;on<2;on++) {
  gale::Engine e;e.prepare(48000);gale::Params p;p.env=env;p.shelter=.6f;p.blow=on;
  float l[256]{},r[256]{};volatile float sum=0;
  auto t=std::clock();
  for(int k=0;k<2000;k++){std::fill_n(l,256,0);std::fill_n(r,256,0);e.process(l,r,256,p);sum+=l[100];}
  std::cout<<env<<","<<on<<","<<(1000.*(std::clock()-t)/CLOCKS_PER_SEC)<<","<<sum<<"\n";
 }
}
