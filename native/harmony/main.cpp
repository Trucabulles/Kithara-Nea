// Standalone CPU prototype. No Python interpreter or subprocess is used.
// LV-Chordia HMM adapted from Music X Lab (MIT); CQT filter tables derived
// from librosa 0.11 (ISC). See accompanying notices and pinned sources.
#define ORT_API_MANUAL_INIT
#include "onnxruntime_cxx_api.h"
#include "json.hpp"
#include "soxr.h"
#include <windows.h>
#include <psapi.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>
#include "../common/CpuThreads.h"
using json=nlohmann::json;
namespace fs=std::filesystem;
using Clock=std::chrono::steady_clock;
constexpr double PI=3.14159265358979323846;
struct Matrix {size_t frames=0, cols=0;std::vector<float> data;};
json read_json(const fs::path& p){std::ifstream f(p);if(!f)throw std::runtime_error("Cannot read "+p.string());json j;f>>j;return j;}
void write_json(const fs::path& p,const json& j){std::ofstream f(p);if(!f)throw std::runtime_error("Cannot write output");f<<j.dump(2);}
double elapsed(Clock::time_point t){return std::chrono::duration<double>(Clock::now()-t).count();}
template<typename T>T read_value(std::istream& f){T v;f.read(reinterpret_cast<char*>(&v),sizeof(v));if(!f)throw std::runtime_error("Truncated input");return v;}
Matrix load_npy(const fs::path& p){
 std::ifstream f(p,std::ios::binary);char magic[6];f.read(magic,6);
 if(std::string(magic,6)!=std::string("\x93NUMPY",6))throw std::runtime_error("Not NPY");
 auto major=read_value<uint8_t>(f);read_value<uint8_t>(f);
 auto n=major==1?read_value<uint16_t>(f):read_value<uint32_t>(f);std::string h(n,' ');f.read(h.data(),n);
 if(h.find("<f4")==std::string::npos||h.find("True")!=std::string::npos)throw std::runtime_error("Require C order float32 NPY");
 auto a=h.find('(',h.find("shape"));auto b=h.find(')',a);auto dims=h.substr(a+1,b-a-1);
 auto comma=dims.find(',');Matrix x;x.frames=std::stoull(dims.substr(0,comma));x.cols=std::stoull(dims.substr(comma+1));
 if(x.cols>1000||x.frames>10000000)throw std::runtime_error("NPY dimensions out of range");
 x.data.resize(x.frames*x.cols);f.read(reinterpret_cast<char*>(x.data.data()),x.data.size()*4);if(!f)throw std::runtime_error("Truncated NPY");return x;
}
void save_npy(const fs::path& p,const Matrix& x){
 std::ofstream f(p,std::ios::binary);f.write("\x93NUMPY\x01\x00",8);
 std::string h="{'descr': '<f4', 'fortran_order': False, 'shape': ("+std::to_string(x.frames)+", "+std::to_string(x.cols)+"), }";
 h+=std::string((64-((10+h.size()+1)%64))%64,' ');h+='\n';uint16_t n=uint16_t(h.size());f.write(reinterpret_cast<char*>(&n),2);f<<h;
 f.write(reinterpret_cast<const char*>(x.data.data()),x.data.size()*4);
}
struct Audio{std::vector<float> samples;int rate=0;};
Audio read_wav(const fs::path& path){
 std::ifstream f(path,std::ios::binary);char riff[4],wave[4];f.read(riff,4);read_value<uint32_t>(f);f.read(wave,4);
 if(std::string(riff,4)!="RIFF"||std::string(wave,4)!="WAVE")throw std::runtime_error("Expected RIFF WAV");
 uint16_t format=0,channels=0,bits=0;Audio audio;std::vector<char> bytes;
 while(f){char name[4];f.read(name,4);if(!f)break;auto length=read_value<uint32_t>(f);auto pos=f.tellg();
  if(std::string(name,4)=="fmt "){format=read_value<uint16_t>(f);channels=read_value<uint16_t>(f);audio.rate=read_value<uint32_t>(f);read_value<uint32_t>(f);read_value<uint16_t>(f);bits=read_value<uint16_t>(f);}
  if(std::string(name,4)=="data"){bytes.resize(length);f.read(bytes.data(),length);}
  f.seekg(pos+std::streamoff(length+(length%2)));
 }
 if(!channels||!audio.rate||bytes.empty())throw std::runtime_error("Empty/unsupported WAV");
 size_t stride=bits/8;audio.samples.resize(bytes.size()/stride/channels);
 for(size_t i=0;i<audio.samples.size();++i){float sum=0;
  for(int c=0;c<channels;++c){auto p=bytes.data()+(i*channels+c)*stride;float v=0;
   if(format==3&&bits==32)memcpy(&v,p,4);
   else if(format==1&&bits==16){int16_t q;memcpy(&q,p,2);v=q/32768.f;}
   else if(format==1&&bits==24){int32_t q=(uint8_t)p[0]|((uint8_t)p[1]<<8)|((int8_t)p[2]<<16);v=q/8388608.f;}
   else if(format==1&&bits==32){int32_t q;memcpy(&q,p,4);v=q/2147483648.f;}
   else throw std::runtime_error("Unsupported WAV encoding");sum+=v;
  }audio.samples[i]=sum/channels;
 }return audio;
}
std::vector<float> resample(const std::vector<float>& input,double in_rate,double out_rate,bool scale){
 if(in_rate==out_rate)return input;
 size_t n=size_t(std::ceil(input.size()*out_rate/in_rate));std::vector<float> out(n+64);size_t used=0,done=0;
 auto io=soxr_io_spec(SOXR_FLOAT32_I,SOXR_FLOAT32_I);auto quality=soxr_quality_spec(SOXR_HQ,0);auto runtime=soxr_runtime_spec(1);
 auto err=soxr_oneshot(in_rate,out_rate,1,input.data(),input.size(),&used,out.data(),out.size(),&done,&io,&quality,&runtime);
 if(err||used!=input.size())throw std::runtime_error(err?err:"Incomplete resampling");
 out.resize(n,0.f);if(scale){double factor=std::sqrt(in_rate/out_rate);for(auto& v:out)v=float(v*factor);}return out;
}
// Double FFT followed by the reference's complex64 STFT cast.
void fft(std::vector<std::complex<double>>& a){
 size_t n=a.size();for(size_t i=1,j=0;i<n;++i){size_t bit=n>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j)std::swap(a[i],a[j]);}
 for(size_t len=2;len<=n;len<<=1){auto wlen=std::polar(1.,-2*PI/len);for(size_t i=0;i<n;i+=len){std::complex<double>w=1.;for(size_t j=0;j<len/2;++j){auto u=a[i+j],v=a[i+j+len/2]*w;a[i+j]=u+v;a[i+j+len/2]=u-v;w*=wlen;}}}
}
std::vector<std::complex<float>> spectrum(const std::vector<float>& y,size_t center,int n,bool hann){
 std::vector<std::complex<double>> a(n);for(int i=0;i<n;++i){int64_t ix=int64_t(center)+i-n/2;
  double v=ix>=0&&ix<int64_t(y.size())?y[size_t(ix)]:0.;if(hann)v*=.5-.5*std::cos(2*PI*i/n);a[i]=v;}
 fft(a);std::vector<std::complex<float>> out(n/2+1);for(int k=0;k<=n/2;++k)out[k]=std::complex<float>(a[k]);return out;
}
int estimate_tuning(const std::vector<float>& y){
 struct Peak{float frequency,magnitude;};std::vector<Peak> peaks;
 for(size_t t=0;t<=y.size()/512;++t){auto spec=spectrum(y,t*512,2048,true);std::vector<float>s(spec.size());float mx=0;
  for(size_t k=0;k<s.size();++k){s[k]=std::abs(spec[k]);mx=std::max(mx,s[k]);}
  for(int k=14;k<372;++k){float a=s[k-1],b=s[k],c=s[k+1];float left=a>.1f*mx?a:0,mid=b>.1f*mx?b:0,right=c>.1f*mx?c:0;
   if(!(mid>left&&mid>=right))continue;
   float denominator=a+c-2*b,shift=0;if(std::abs(c-a)<std::abs(2*denominator)&&denominator!=0)shift=.5f*(a-c)/denominator;
   float freq=float((k+shift)*22050./2048.);float mag=b+.25f*(c-a)*shift;peaks.push_back({freq,mag});}
 }
 if(peaks.empty())return 50;
 std::vector<float> mags;for(auto p:peaks)mags.push_back(p.magnitude);size_t m=mags.size()/2;std::nth_element(mags.begin(),mags.begin()+m,mags.end());float median=mags[m];
 if(mags.size()%2==0)median=.5f*(median+*std::max_element(mags.begin(),mags.begin()+m));
 std::array<size_t,100> histogram{};
 for(auto p:peaks)if(p.magnitude>=median){
  // NumPy keeps piptrack frequencies and hz_to_octs in float32.
  float oct=std::log2(p.frequency/27.5f);float residual=std::fmod(36.f*oct,1.f);if(residual<0)residual+=1;if(residual>=.5f)residual-=1;
  int bin=int(std::floor((double(residual)+.5)*100));if(bin>=0&&bin<100)++histogram[bin];}
 return int(std::max_element(histogram.begin(),histogram.end())-histogram.begin());
}
Matrix cqt(const std::vector<float>& y,const fs::path& assets,int& tuning){
 tuning=estimate_tuning(y);char name[32];snprintf(name,sizeof(name),"cqt-%02d.json",tuning);auto bank=read_json(assets/name);
 Matrix output;output.frames=1+y.size()/512;output.cols=288;output.data.resize(output.frames*288);
 std::map<int,std::vector<float>> rates;rates[1]=y;
 for(auto& stage:bank["stages"]){int factor=stage["factor"],n=stage["n_fft"],hop=stage["hop"],begin=stage["begin"];bool phase=stage["phase"];
  if(!rates.count(factor)){int previous=1;for(auto& pair:rates)if(pair.first<factor)previous=pair.first;
   rates[factor]=resample(rates.at(previous),double(factor)/previous,1,true);}
  auto& signal=rates.at(factor);output.frames=std::min(output.frames,1+signal.size()/hop);
  struct Coeff{int index;std::complex<float> value;};struct Row{double scale;std::vector<Coeff> coefficients;};std::vector<Row> rows;
  for(auto& r:stage["rows"]){Row row;row.scale=r["scale"];for(auto& e:r["entries"])row.coefficients.push_back({e[0],{e[1].get<float>(),e[2].get<float>()}});rows.push_back(std::move(row));}
  for(size_t t=0;t<output.frames;++t){auto spec=spectrum(signal,t*hop,n,!phase);
   if(!phase)for(auto& v:spec)v={std::abs(v),0};
   for(size_t b=0;b<rows.size();++b){std::complex<float> sum=0;for(auto& e:rows[b].coefficients)sum+=spec[e.index]*e.value;
    // Full CQT divides the complex response by sqrt(length) before abs.
    auto normalized=std::complex<float>(float(sum.real()*rows[b].scale),float(sum.imag()*rows[b].scale));
    output.data[t*288+begin+b]=phase?std::abs(normalized):float(sum.real()*rows[b].scale);}
  }
 }
 output.data.resize(output.frames*288);return output;
}
struct Segment{double start,end;std::string name;};
using Probabilities=std::array<Matrix,6>;
std::vector<Segment> decode(const Probabilities& probs,const fs::path& assets){
 struct State{std::array<int,6> index;std::string name;};std::vector<State> states;
 for(auto& r:read_json(assets/"dictionary.json")){State state;bool valid=true;
  for(int h=0;h<6;++h){state.index[h]=r["indices"][h];if(state.index[h]>=int(probs[h].cols))valid=false;}
  state.index[1]++;state.name=r["name"];if(valid)states.push_back(state);}
 auto frames=probs[0].frames,n=states.size();if(!frames||!n)throw std::runtime_error("Empty HMM");
 std::vector<float> previous(n,-std::numeric_limits<float>::infinity()),current(n);previous[0]=0;
 std::vector<int32_t> back(frames*n,-1);int best=0;
 for(size_t t=0;t<frames;++t){float diff=t?previous[best]-30.f:-std::numeric_limits<float>::infinity();int next_best=0;
  for(size_t s=0;s<n;++s){auto& state=states[s];float observation=0;
   for(int h=0;h<6;++h){auto ix=state.index[h];if(ix>=0)observation+=std::log(probs[h].data[t*probs[h].cols+ix]);}
   float value=previous[s];if(t){back[t*n+s]=value>diff?int32_t(s):best;value=std::max(value,diff);}current[s]=value+observation;
   if(current[s]>current[next_best])next_best=int(s);
  }previous.swap(current);best=next_best;
 }
 std::vector<int> tags(frames);tags.back()=best;for(size_t t=frames-1;t>0;--t)tags[t-1]=back[t*n+tags[t]];
 std::vector<Segment> segments;size_t start=0;
 for(size_t t=0;t<frames;++t)if(t+1==frames||tags[t]!=tags[t+1]){segments.push_back({start*512./22050,(t+1)*512./22050,states[tags[t]].name});start=t+1;}
 return segments;
}
Probabilities infer(Matrix& features,const fs::path& models){
 if(features.cols!=288||features.frames==0)throw std::runtime_error("Invalid CQT shape");
 Ort::Env env(ORT_LOGGING_LEVEL_WARNING,"kithara-lv-native");Ort::SessionOptions options;options.SetIntraOpNumThreads(kn::analysisCpuThreads());options.SetInterOpNumThreads(1);options.DisableCpuMemArena();options.DisableMemPattern();
 auto memory=Ort::MemoryInfo::CreateCpu(OrtArenaAllocator,OrtMemTypeDefault);int64_t dims[]={int64_t(features.frames),288};
 auto input=Ort::Value::CreateTensor<float>(memory,features.data.data(),features.data.size(),dims,2);
 const char* input_names[]={"cqt"};const char* output_names[]={"triad","bass","seventh","ninth","eleventh","thirteenth"};Probabilities mean;
 for(int m=0;m<5;++m){auto path=models/("chordnet-s"+std::to_string(m)+".onnx");Ort::Session session(env,path.c_str(),options);
  auto outputs=session.Run(Ort::RunOptions{nullptr},input_names,&input,1,output_names,6);
  for(int h=0;h<6;++h){auto shape=outputs[h].GetTensorTypeAndShapeInfo().GetShape();auto& mat=mean[h];if(!m){mat.frames=size_t(shape[0]);mat.cols=size_t(shape[1]);mat.data.resize(mat.frames*mat.cols);}
   auto ptr=outputs[h].GetTensorData<float>();for(size_t k=0;k<mat.data.size();++k)mat.data[k]+=ptr[k];}
  std::cerr<<"Network "<<m+1<<"/5\n";
 }for(auto& head:mean)for(auto& v:head.data)v/=5.f;return mean;
}
int wmain(int argc,wchar_t** argv){try{
 if(argc<5){std::cerr<<"Usage: kithara-lv-native --audio|--cqt|--features input output assets [models]\n";return 2;}
 fs::path input=argv[2],output=argv[3],assets=argv[4];std::wstring mode=argv[1];auto start=Clock::now();Matrix features;int tuning=-1;double cqt_seconds=0;
 if(mode==L"--features")features=load_npy(input);
 else if(mode==L"--audio"||mode==L"--cqt"){auto audio=read_wav(input);auto y=resample(audio.samples,audio.rate,22050,false);features=cqt(y,assets,tuning);cqt_seconds=elapsed(start);}
 else throw std::runtime_error("Unknown mode");
 if(mode==L"--cqt"){save_npy(output,features);write_json(output.wstring()+L".json",{{"tuning_index",tuning},{"frames",features.frames},{"seconds",cqt_seconds}});return 0;}
 if(mode==L"--audio")save_npy(output.wstring()+L".cqt.npy",features);
 if(argc!=6)throw std::runtime_error("Models directory required");
 auto library=LoadLibraryW(L"onnxruntime.dll");if(!library)throw std::runtime_error("Cannot load native onnxruntime.dll");
 auto get_api=reinterpret_cast<const OrtApiBase*(ORT_API_CALL*)()>(GetProcAddress(library,"OrtGetApiBase"));if(!get_api)throw std::runtime_error("Missing OrtGetApiBase");
 Ort::InitApi(get_api()->GetApi(ORT_API_VERSION));auto network_start=Clock::now();auto probs=infer(features,fs::path(argv[5]));double network_seconds=elapsed(network_start);
 for(int h=0;h<6;++h)save_npy(output.wstring()+L".head"+std::to_wstring(h)+L".npy",probs[h]);
 auto hmm_start=Clock::now();auto chords=decode(probs,assets);double hmm_seconds=elapsed(hmm_start);json result=json::array();
 for(auto& s:chords)result.push_back({{"start_time",std::nearbyint(s.start*100)/100},{"end_time",std::nearbyint(s.end*100)/100},{"chord",s.name}});
 write_json(output,result);PROCESS_MEMORY_COUNTERS counters{};GetProcessMemoryInfo(GetCurrentProcess(),&counters,sizeof(counters));
 write_json(output.wstring()+L".metrics.json",{{"frames",features.frames},{"tuning_index",tuning},{"cqt_seconds",cqt_seconds},{"network_seconds",network_seconds},{"hmm_seconds",hmm_seconds},{"total_seconds",elapsed(start)},{"peak_process_bytes",counters.PeakWorkingSetSize},{"python_runtime",false},{"onnxruntime",get_api()->GetVersionString()},{"device","cpu"}});
 return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
