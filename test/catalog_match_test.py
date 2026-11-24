"""Compile the actual metadata/matching functions with a small Arduino String double."""
from pathlib import Path
import subprocess,tempfile
base=Path(__file__).resolve().parents[1];s=(base/'display/src/network.cpp').read_text()
code=s[s.index('String normalized('):s.index('class LimitedStream')]
stub=r'''
#include "lyrics_core.h"
#include "metadata_policy.h"
#include <string>
#include <cctype>
#include <cmath>
#include <cassert>
#include <iostream>
class String:public std::string{
public:using std::string::string;String(const std::string&s):std::string(s){}String()=default;
 char*begin(){return data();}size_t length()const{return size();}
 void toLowerCase(){for(auto&c:*this)c=char(std::tolower(uint8_t(c)));}
 void trim(){while(!empty()&&std::isspace(uint8_t(back())))pop_back();size_t at=0;while(at<size()&&std::isspace(uint8_t((*this)[at])))++at;erase(0,at);}
 void setCharAt(size_t n,char c){(*this)[n]=c;}
 int indexOf(const char*s,size_t at=0)const{auto n=find(s,at);return n==npos?-1:int(n);}
 int indexOf(const String&s,size_t at=0)const{return indexOf(s.c_str(),at);}
 int indexOf(char c,size_t at=0)const{auto n=find(c,at);return n==npos?-1:int(n);}
 void replace(const char*a,const char*b){size_t at=0;while((at=find(a,at))!=npos){std::string::replace(at,strlen(a),b);at+=strlen(b);}}
 void remove(size_t at,size_t n=npos){erase(at,n);}String substring(size_t a,size_t b)const{return substr(a,b-a);}
};
String operator+(const char*a,const String&b){return String(std::string(a)+b.c_str());}
String operator+(const String&a,const char*b){return String(std::string(a.c_str())+b);}
String operator+(const String&a,const String&b){return String(std::string(a.c_str())+b.c_str());}
struct Track{char title[192]{},artist[128]{};uint32_t duration=0;};
'''
# Range-based iteration above must use the std::string iterator, not Arduino's char* begin().
stub=stub.replace('for(auto&c:*this)c=char(std::tolower(uint8_t(c)));','for(size_t i=0;i<size();++i)(*this)[i]=char(std::tolower(uint8_t((*this)[i])));')
checks=r'''
int main(){
 Track t;strcpy(t.title,"Vai Lan Don Dua");strcpy(t.artist,"SOOBIN");t.duration=170667;
 assert(matchScore("Vài Lần Đón Đưa","SOOBIN",170.7,t)>.99f);
 assert(matchScore("Vài Lần Đón Đưa (Live)","SOOBIN",170.7,t)<0);
 assert(matchScore("Vài Lần Đón Đưa","Other Artist",170.7,t)<0);
 assert(matchScore("Vài Lần Đón Đưa","SOOBIN",190,t)<0);
 assert(matchScore("Tháng Năm","SOOBIN",170.7,t)<0);
 strcpy(t.title,"Bài Ca Tuổi Trẻ");strcpy(t.artist,"Artist A, Artist B");t.duration=180000;
 assert(matchScore("Bai Ca Tuoi Tre","Artist A",180,t)>.99f);
 assert(normalized("ĐĂNG  --  KHOA")=="dang khoa");
 assert(cleaned("Bài Ca (feat. Guest) (Live)")=="Bài Ca (Live)");
 std::cout<<"Actual catalog matching: ASCII/accented Vietnamese, primary artist, wrong song/artist/version/duration rejection: PASS\n";
}
'''
with tempfile.TemporaryDirectory() as temp:
 p=Path(temp);(p/'match.cpp').write_text(stub+code+checks)
 subprocess.run(['c++','-std=c++17','-fsanitize=address,undefined','-I'+str(base/'shared'),str(p/'match.cpp'),'-o',str(p/'match')],check=True)
 subprocess.run([str(p/'match')],check=True)
