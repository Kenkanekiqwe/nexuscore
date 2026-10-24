#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

namespace {
using Clock = std::chrono::steady_clock;
enum class Module { Particles, Renderer, Network, Performance };
struct Particle { float x, y, vx, vy; };
struct Packet { double time; std::string src, dst, proto; int bytes; };
struct State {
    Module active = Module::Particles;
    std::vector<Particle> particles;
    std::vector<Packet> packets;
    int particleCount = 2400;
    float gravity = 18.0f, force = 160.0f, rotation = 0.0f, particleSize = 1.5f;
    float networkZoom = 1.0f, networkPanX = 0.0f, networkPanY = 0.0f;
    float cameraZoom = 1.0f, cameraYaw = 0.0f, cameraPitch = 0.12f;
    bool networkPhysics = true, networkLabels = true, rendererStars = true, rendererGlow = true;
    bool paused = false, grid = true, trails = false, autoRotate = true, demoTraffic = true;
    double frameMs = 0.0, packetRate = 0.0, bytesTotal = 0.0;
    std::uint64_t packetTotal = 0, frameTotal = 0;
    Clock::time_point lastPacket = Clock::now();
    std::array<float, 90> frameHistory{};
    std::size_t historyIndex = 0;
};
const ImVec4 C_BG{0.027f,0.035f,0.050f,1};
const ImVec4 C_PANEL{0.043f,0.055f,0.075f,1};
const ImVec4 C_PANEL2{0.060f,0.076f,0.099f,1};
const ImVec4 C_LINE{0.12f,0.16f,0.21f,1};
const ImVec4 C_TEXT{0.87f,0.91f,0.95f,1};
const ImVec4 C_MUTED{0.40f,0.48f,0.57f,1};
const ImVec4 C_TEAL{0.24f,0.84f,0.83f,1};
const ImVec4 C_BLUE{0.32f,0.62f,0.98f,1};
const ImVec4 C_GREEN{0.35f,0.83f,0.61f,1};
const ImVec4 C_AMBER{1.0f,0.70f,0.31f,1};

void resetParticles(State& s) {
    s.particles.clear();
    s.particles.reserve(static_cast<std::size_t>(s.particleCount));
    std::mt19937 rng(0x4E455855u + static_cast<unsigned>(s.particleCount));
    std::uniform_real_distribution<float> p(-1.0f, 1.0f), v(-25.0f, 25.0f);
    for (int i=0; i<s.particleCount; ++i) s.particles.push_back({p(rng),p(rng),v(rng),v(rng)});
}
void style() {
    ImGui::StyleColorsDark();
    auto& st=ImGui::GetStyle();
    st.WindowRounding=0; st.ChildRounding=7; st.FrameRounding=5; st.PopupRounding=6;
    st.ScrollbarRounding=8; st.GrabRounding=5; st.WindowPadding={12,11}; st.FramePadding={10,7};
    st.ItemSpacing={9,8}; st.CellPadding={9,7}; st.WindowBorderSize=0; st.ChildBorderSize=1;
    st.FrameBorderSize=0; st.ScrollbarSize=11;
    auto* c=st.Colors;
    c[ImGuiCol_WindowBg]=C_BG; c[ImGuiCol_ChildBg]=C_PANEL; c[ImGuiCol_PopupBg]=C_PANEL2;
    c[ImGuiCol_Border]=C_LINE; c[ImGuiCol_Separator]=C_LINE;
    c[ImGuiCol_Text]=C_TEXT; c[ImGuiCol_TextDisabled]=C_MUTED;
    c[ImGuiCol_FrameBg]=C_PANEL2; c[ImGuiCol_FrameBgHovered]={0.09f,0.14f,0.18f,1};
    c[ImGuiCol_FrameBgActive]={0.10f,0.19f,0.22f,1};
    c[ImGuiCol_Button]={0.08f,0.16f,0.19f,1}; c[ImGuiCol_ButtonHovered]={0.10f,0.28f,0.31f,1};
    c[ImGuiCol_ButtonActive]={0.08f,0.36f,0.38f,1}; c[ImGuiCol_Header]={0.07f,0.26f,0.29f,1};
    c[ImGuiCol_HeaderHovered]={0.08f,0.30f,0.34f,1}; c[ImGuiCol_CheckMark]=C_TEAL;
    c[ImGuiCol_SliderGrab]=C_TEAL; c[ImGuiCol_SliderGrabActive]={0.42f,0.95f,0.91f,1};
    c[ImGuiCol_Tab]={0.05f,0.09f,0.12f,1}; c[ImGuiCol_TabHovered]={0.08f,0.28f,0.31f,1};
}
void sectionLabel(const char* text) {
    ImGui::TextColored(C_MUTED,"%s",text);
}
void metric(const char* label, const char* value, const char* unit, ImVec4 accent=C_TEAL) {
    ImGui::BeginGroup();
    ImGui::TextColored(C_MUTED,"%s",label);
    ImGui::Spacing();
    ImGui::TextColored(accent,"%s",value);
    if(unit && unit[0]) { ImGui::SameLine(0,4); ImGui::TextColored(C_MUTED,"%s",unit); }
    ImGui::EndGroup();
}
void moduleButton(const char* glyph, const char* label, const char* hint, Module m, State& s) {
    const bool selected=s.active==m;
    if(selected) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg,ImVec4(0.07f,0.20f,0.22f,1));
        ImGui::PushStyleColor(ImGuiCol_Border,ImVec4(0.13f,0.48f,0.49f,1));
    } else {
        ImGui::PushStyleColor(ImGuiCol_ChildBg,ImVec4(0.043f,0.055f,0.075f,1));
        ImGui::PushStyleColor(ImGuiCol_Border,ImVec4(0.043f,0.055f,0.075f,1));
    }
    ImGui::BeginChild(label,ImVec2(-1,58),ImGuiChildFlags_Borders);
    ImGui::SetCursorPos({11,10});
    ImGui::PushStyleColor(ImGuiCol_Button,selected?ImVec4(0.09f,0.34f,0.36f,1):ImVec4(0.08f,0.11f,0.15f,1));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,ImVec4(0.12f,0.30f,0.33f,1));
    if(ImGui::Button(glyph,ImVec2(35,35))) s.active=m;
    ImGui::PopStyleColor(2);
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY()+1);
    if(ImGui::Selectable(label,selected,0,ImVec2(125,20))) s.active=m;
    ImGui::TextColored(C_MUTED,"%s",hint);
    ImGui::EndGroup();
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
}
void drawParticles(State& s) {
    ImGui::TextColored(C_MUTED,"PARTICLE FIELD  /  PROCEDURAL FLOW");
    ImGui::SameLine(); ImGui::TextColored(C_AMBER,"GOLDEN NEBULA");
    ImGui::Separator();
    ImVec2 a=ImGui::GetContentRegionAvail(), canvas(std::max(1.0f,a.x),std::max(180.0f,a.y));
    ImGui::InvisibleButton("particle-canvas",canvas,ImGuiButtonFlags_MouseButtonLeft);
    ImVec2 o=ImGui::GetItemRectMin(), z=ImGui::GetItemRectSize(); auto*d=ImGui::GetWindowDrawList();
    d->AddRectFilled(o,{o.x+z.x,o.y+z.y},IM_COL32(2,3,5,255),5);
    const float t=float(ImGui::GetTime()), w=z.x, h=z.y;
    // Three overlapping density fields create the luminous, diagonal nebula shown in the reference.
    for(int i=0;i<std::min(s.particleCount,20000);++i) {
        const float fi=float(i), seed=fi*12.9898f+78.233f;
        float u=std::fmod(std::sin(seed)*43758.5453f+1.0f,1.0f); if(u<0)u+=1;
        float v=std::fmod(std::sin(seed*1.371f)*24634.6345f+1.0f,1.0f); if(v<0)v+=1;
        float along=u;
        float centerX=w*(.12f+.78f*along)+std::sin(along*11.0f+t*.12f)*w*.055f;
        float centerY=h*(.12f+.78f*along)+std::sin(along*15.0f+t*.17f)*h*.065f;
        float spread=(.018f+.19f*std::exp(-std::pow((along-.43f)*4.0f,2)))*std::min(w,h);
        float side=(v-.5f)*spread*2.8f;
        float x=centerX+side*.78f+(v-.5f)*w*.09f;
        float y=centerY-side*.42f+(v-.5f)*h*.07f;
        // A second broad spiral stream and a dense central cloud break the silhouette into filaments.
        x+=std::sin(v*31.0f+along*18.0f+t*.18f)*spread*.24f;
        y+=std::cos(v*23.0f-along*12.0f+t*.15f)*spread*.20f;
        if(x<o.x+2||x>o.x+w-2||y<o.y+2||y>o.y+h-2) continue;
        float core=std::exp(-std::pow((along-.44f)*5.2f,2))*std::exp(-std::pow((v-.52f)*3.4f,2));
        int alpha=std::clamp(int(45+core*190+(u>.93f?70:0)),20,255);
        float r=(u>.987f?1.7f:0.45f)+core*1.65f;
        ImU32 color=IM_COL32(255,int(184+55*core),int(100+100*core),alpha);
        if(core>.48f && (i%4)==0) d->AddCircleFilled({x,y},r*3.2f,IM_COL32(255,186,93,22));
        d->AddCircleFilled({x,y},r,color);
    }
    // A few bright particle clusters give the field scale and depth.
    for(int k=0;k<9;++k) {
        float px=o.x+w*(.12f+std::fmod(k*.173f+.09f, .78f));
        float py=o.y+h*(.18f+std::fmod(k*.317f+.03f, .68f));
        float pulse=1.0f+.2f*std::sin(t*1.4f+k);
        d->AddCircle({px,py},3.0f*pulse,IM_COL32(255,221,155,100),12,1.0f);
        d->AddCircleFilled({px,py},1.2f*pulse,IM_COL32(255,231,179,230));
    }
    d->AddText({o.x+16,o.y+14},IM_COL32(180,153,112,210),"VOID FIELD  /  PROCEDURAL PARTICLE DENSITY");
    d->AddText({o.x+16,o.y+h-25},IM_COL32(125,111,91,190),s.paused?"FIELD FROZEN":"LIVE FIELD  -  DEPTH / FILAMENT / CLUSTER");
    d->AddRect(o,{o.x+w,o.y+h},IM_COL32(54,43,29,170),5);
}
void drawRenderer(State& s) {
    ImGui::TextColored(C_MUTED,"VOID ENGINE  /  3D STELLAR FIELD");
    ImGui::SameLine(); ImGui::TextColored(C_AMBER,"PERSPECTIVE PARTICLE VOLUME");
    ImGui::Separator();
    ImVec2 a=ImGui::GetContentRegionAvail(), canvas(std::max(1.0f,a.x),std::max(180.0f,a.y));
    ImGui::InvisibleButton("renderer-canvas",canvas,ImGuiButtonFlags_MouseButtonLeft|ImGuiButtonFlags_MouseButtonRight);
    ImVec2 o=ImGui::GetItemRectMin(), z=ImGui::GetItemRectSize(); auto*d=ImGui::GetWindowDrawList();
    ImGuiIO& io=ImGui::GetIO();
    if(ImGui::IsItemHovered()) {
        if(io.MouseWheel!=0) s.cameraZoom=std::clamp(s.cameraZoom+io.MouseWheel*.12f,.35f,3.5f);
        if(ImGui::IsMouseDragging(ImGuiMouseButton_Left)) { s.cameraYaw+=io.MouseDelta.x*.006f; s.cameraPitch=std::clamp(s.cameraPitch+io.MouseDelta.y*.005f,-1.2f,1.2f); }
    }
    d->AddRectFilled(o,{o.x+z.x,o.y+z.y},IM_COL32(1,2,4,255),5);
    const float t=float(ImGui::GetTime()), cx=o.x+z.x*.5f, cy=o.y+z.y*.51f;
    if(s.grid) {
        for(int i=-12;i<=12;++i) {
            float xx=cx+i*z.x/24.0f;
            d->AddLine({xx,o.y+z.y*.12f},{cx+i*z.x*.72f,o.y+z.y},IM_COL32(83,65,42,42));
        }
        for(int i=1;i<14;++i) {
            float v=float(i)/14.0f, yy=o.y+z.y*(.43f+v*v*.58f);
            d->AddLine({o.x,yy},{o.x+z.x,yy},IM_COL32(83,65,42,int(10+v*30)));
        }
    }
    // A deterministic 3D cloud, projected with perspective. The orbit and depth changes are animated.
    const int count=std::clamp(s.particleCount,100,20000);
    for(int i=0;i<count;++i) {
        float fi=float(i);
        float u=std::fmod(std::sin(fi*127.1f+311.7f)*43758.5453f+1.0f,1.0f); if(u<0)u+=1;
        float v=std::fmod(std::sin(fi*269.5f+183.3f)*22578.1459f+1.0f,1.0f); if(v<0)v+=1;
        float w=std::fmod(std::sin(fi*73.7f+91.9f)*13579.246f+1.0f,1.0f); if(w<0)w+=1;
        float radius=std::pow(u,.72f)*2.7f;
        float ang=v*6.2831853f + t*(.035f+u*.045f);
        float x=std::cos(ang)*radius + .22f*std::sin(w*16.0f+t*.18f);
        float y=(w-.5f)*3.6f + .22f*std::sin(ang*2.0f+t*.12f);
        float zz=std::sin(ang)*radius + (u-.5f)*1.8f;
        float yaw=s.cameraYaw+(s.autoRotate?t*.055f:0.0f), cyaw=std::cos(yaw), syaw=std::sin(yaw);
        float rx=x*cyaw-zz*syaw, rz=x*syaw+zz*cyaw+5.0f;
        float cp=std::cos(s.cameraPitch), sp=std::sin(s.cameraPitch);
        float ry=y*cp-rz*sp, depth=y*sp+rz*cp;
        if(depth<.35f) continue;
        float focal=std::min(z.x,z.y)*.72f*s.cameraZoom;
        float px=cx+rx/depth*focal, py=cy-ry/depth*focal;
        if(px<o.x+2||px>o.x+z.x-2||py<o.y+2||py>o.y+z.y-2) continue;
        float core=std::exp(-(rx*rx+ry*ry)*.23f);
        float twinkle=.55f+.45f*std::sin(t*(1.0f+u*4.0f)+fi*.71f);
        int alpha=std::clamp(int((45+core*170)*twinkle*(1.1f/depth)),20,255);
        float r=std::clamp((.35f+core*1.25f)*s.cameraZoom*(2.8f/depth),.35f,3.6f);
        if(s.rendererGlow && r>1.1f) d->AddCircleFilled({px,py},r*4.2f,IM_COL32(255,170,73,18));
        if(s.rendererGlow && r>1.6f) d->AddCircleFilled({px,py},r*2.0f,IM_COL32(255,197,111,32));
        d->AddCircleFilled({px,py},r,IM_COL32(255,int(174+70*core),int(91+80*core),alpha));
    }
    // Brighter foreground star clusters, with spherical wireframe hints.
    for(int k=0;k<7;++k) {
        float ang=t*.08f+k*2.39996f, rad=.22f+float(k%4)*.12f;
        float px=cx+std::cos(ang)*z.x*(.08f+rad*.35f);
        float py=cy+std::sin(ang*1.3f)*z.y*(.12f+rad*.35f);
        float pulse=1.0f+.22f*std::sin(t*1.8f+k);
        if(s.rendererGlow) d->AddCircleFilled({px,py},9*pulse,IM_COL32(255,190,104,22));
        d->AddCircleFilled({px,py},2.1f*pulse,IM_COL32(255,238,194,250));
        d->AddCircle({px,py},5.0f*pulse,IM_COL32(255,220,160,105),18,1.0f);
    }
    d->AddText({o.x+14,o.y+12},IM_COL32(195,157,104,220),"VOID / 3D DEPTH FIELD");
    d->AddText({o.x+14,o.y+z.y-24},IM_COL32(151,127,94,220),"DRAG: ORBIT CAMERA   WHEEL: ZOOM   |   GPU-STYLE CPU PREVIEW");
    d->AddRect(o,{o.x+z.x,o.y+z.y},IM_COL32(65,48,28,190),5);
}
void synthesize(State& s) {
    auto now=Clock::now(); double elapsed=std::chrono::duration<double>(now-s.lastPacket).count();
    if(!s.demoTraffic || elapsed<.12) return;
    s.lastPacket=now; static std::mt19937 rng(0x4E455453u);
    static const std::array<const char*,4> protos{"TCP","UDP","DNS","TLS"};
    std::uniform_int_distribution<int> ip(2,250),pr(0,3),sz(64,1450),npack(1,5); int n=npack(rng);
    for(int i=0;i<n;++i) {
        Packet p{ImGui::GetTime(),"192.168.1."+std::to_string(ip(rng)),"10.0.0."+std::to_string(ip(rng)),protos[static_cast<std::size_t>(pr(rng))],sz(rng)};
        s.bytesTotal+=p.bytes; ++s.packetTotal; s.packets.push_back(std::move(p));
    }
    while(s.packets.size()>250) s.packets.erase(s.packets.begin());
    s.packetRate=n/elapsed;
}
double memoryMb() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS c{}; c.cb=sizeof(c);
    if(GetProcessMemoryInfo(GetCurrentProcess(),&c,sizeof(c))) return double(c.WorkingSetSize)/(1024.0*1024.0);
#endif
    return -1;
}
void drawNetwork(State& s) {
    ImGui::TextColored(C_MUTED,"NETWORK VISUALIZATION");
    ImGui::SameLine(); ImGui::TextColored(C_AMBER,"209 NODES / 627 CONNECTIONS");
    ImGui::SameLine(); ImGui::TextColored(C_MUTED,"drag: pan  |  wheel: zoom");
    ImGui::Separator();
    ImVec2 a=ImGui::GetContentRegionAvail(), canvas(std::max(1.0f,a.x),std::max(180.0f,a.y));
    ImGui::InvisibleButton("network-canvas",canvas,ImGuiButtonFlags_MouseButtonLeft|ImGuiButtonFlags_MouseButtonRight|ImGuiButtonFlags_MouseButtonMiddle);
    ImVec2 o=ImGui::GetItemRectMin(), z=ImGui::GetItemRectSize(); auto*d=ImGui::GetWindowDrawList();
    ImGuiIO& io=ImGui::GetIO();
    if(ImGui::IsItemHovered()) {
        if(io.MouseWheel!=0) s.networkZoom=std::clamp(s.networkZoom*std::pow(1.12f,io.MouseWheel),.28f,4.5f);
        if(ImGui::IsMouseDragging(ImGuiMouseButton_Left)||ImGui::IsMouseDragging(ImGuiMouseButton_Middle)||ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
            s.networkPanX+=io.MouseDelta.x/std::max(1.0f,z.x);
            s.networkPanY+=io.MouseDelta.y/std::max(1.0f,z.y);
        }
    }
    d->AddRectFilled(o,{o.x+z.x,o.y+z.y},IM_COL32(12,12,13,255),4);
    for(float gx=0;gx<z.x;gx+=22) d->AddLine({o.x+gx,o.y},{o.x+gx,o.y+z.y},IM_COL32(70,70,70,34));
    for(float gy=0;gy<z.y;gy+=22) d->AddLine({o.x,o.y+gy},{o.x+z.x,o.y+gy},IM_COL32(70,70,70,34));
    struct Node { float x,y,phase,speed; int type; };
    static std::array<Node,209> nodes{};
    static std::array<std::array<int,2>,627> edges{};
    static bool initialized=false;
    if(!initialized) {
        std::mt19937 rng(0x4E455855u); std::uniform_real_distribution<float> j(-.055f,.055f), ph(0.0f,6.28318f), sp(.35f,1.2f);
        for(int i=0;i<209;++i) {
            float ang=float(i)*2.39996323f, rad=std::sqrt(float(i)/209.0f);
            nodes[i]={.5f+std::cos(ang)*rad*.43f+j(rng),.5f+std::sin(ang)*rad*.42f+j(rng),ph(rng),sp(rng),(i%31==0)?1:((i%47==0)?2:0)};
        }
        for(int i=0;i<209;++i) for(int k=1;k<=3;++k) edges[i*3+k-1]={i,(i+k*17+13)%209};
        initialized=true;
    }
    const float t=float(ImGui::GetTime());
    auto pos=[&](int i) {
        const Node& n=nodes[static_cast<std::size_t>(i)];
        float x=n.x+std::sin(t*n.speed+n.phase)*.012f;
        float y=n.y+std::cos(t*n.speed*.83f+n.phase*1.31f)*.014f;
        return ImVec2{o.x+z.x*(.5f+(x-.5f)*s.networkZoom+s.networkPanX),
                      o.y+z.y*(.5f+(y-.5f)*s.networkZoom+s.networkPanY)};
    };
    for(std::size_t i=0;i<edges.size();++i) {
        ImVec2 p1=pos(edges[i][0]),p2=pos(edges[i][1]);
        int alpha=std::clamp(int(28+22*(.5f+.5f*std::sin(t*1.2f+float(i)*.17f))),20,52);
        d->AddLine(p1,p2,IM_COL32(190,195,202,alpha),.75f);
        if(i%13==0) {
            float pulse=std::fmod(t*.22f+float(i)*.071f,1.0f);
            ImVec2 pp{p1.x+(p2.x-p1.x)*pulse,p1.y+(p2.y-p1.y)*pulse};
            d->AddCircleFilled(pp,1.8f,IM_COL32(255,218,150,185));
        }
    }
    const char* hubNames[]={"TCP","Catalyst 9000","ASA 5500","UDP","SIX Series","ICMP","Command Injection","Login Attempt","Data Exfiltration"};
    const ImVec2 hubPos[]={{.50f,.08f},{.17f,.35f},{.82f,.33f},{.08f,.83f},{.51f,.82f},{.90f,.82f},{.50f,.35f},{.40f,.65f},{.63f,.66f}};
    const ImU32 hubColors[]={IM_COL32(255,139,0,255),IM_COL32(43,170,53,255),IM_COL32(43,170,53,255),IM_COL32(255,126,0,255),IM_COL32(43,170,53,255),IM_COL32(255,126,0,255),IM_COL32(222,39,43,255),IM_COL32(222,39,43,255),IM_COL32(222,39,43,255)};
    auto hubScreen=[&](int h) {
        return ImVec2{o.x+z.x*(.5f+(hubPos[h].x-.5f)*s.networkZoom+s.networkPanX),
                      o.y+z.y*(.5f+(hubPos[h].y-.5f)*s.networkZoom+s.networkPanY)};
    };
    for(int h=0;h<9;++h) {
        ImVec2 hp=hubScreen(h);
        for(int i=h*7%209;i<209;i+=3) d->AddLine(hp,pos(i),IM_COL32(215,215,215,45),.8f);
    }
    for(int i=0;i<209;++i) {
        ImVec2 p=pos(i);
        if(p.x<o.x-10||p.x>o.x+z.x+10||p.y<o.y-10||p.y>o.y+z.y+10) continue;
        float pulse=.75f+.25f*std::sin(t*2.0f+nodes[i].phase);
        d->AddCircleFilled(p,5.8f*pulse,IM_COL32(24,132,204,28));
        d->AddCircleFilled(p,3.0f*pulse,IM_COL32(40,155,224,235));
        d->AddCircleFilled(p,1.2f,IM_COL32(171,225,255,245));
        if(s.networkLabels && i%7==0 && s.networkZoom>.65f) {
            char label[32]; std::snprintf(label,sizeof(label),"%03d.%03d.%03d.%03d",(i*17+11)%255,(i*29+43)%255,(i*13+7)%255,(i*31+19)%255);
            d->AddText({p.x+4,p.y-6},IM_COL32(220,225,230,190),label);
        }
    }
    for(int h=0;h<9;++h) {
        ImVec2 p=hubScreen(h); float pulse=1.0f+.08f*std::sin(t*2.2f+h);
        float r=((h==0||h>=6)?22.0f:19.0f)*std::clamp(s.networkZoom,.7f,1.6f)*pulse;
        ImU32 col=hubColors[h];
        int cr=(h==0||h==3||h==5)?255:((h==1||h==2||h==4)?43:222);
        int cg=(h==1||h==2||h==4)?170:((h>=6)?39:126);
        int cb=(h==1||h==2||h==4)?53:((h>=6)?43:0);
        d->AddCircleFilled(p,r+12,IM_COL32(cr,cg,cb,22));
        d->AddCircleFilled(p,r+5,IM_COL32(cr,cg,cb,35));
        d->AddCircleFilled(p,r,col);
        ImVec2 ts=ImGui::CalcTextSize(hubNames[h]);
        d->AddText({p.x-ts.x*.5f,p.y-ts.y*.5f},IM_COL32(255,255,255,255),hubNames[h]);
    }
    d->AddText({o.x+12,o.y+10},IM_COL32(100,175,220,230),"209 NODES   /   627 CONNECTIONS");
    d->AddText({o.x+12,o.y+z.y-21},IM_COL32(130,130,130,220),"Pan: drag   Zoom: wheel   |   Moving nodes / synthetic graph");
    d->AddRect(o,{o.x+z.x,o.y+z.y},IM_COL32(46,46,46,220),4);
}
void drawPerformance(State& s) {
    const double fps=s.frameMs>0?1000.0/s.frameMs:0, mem=memoryMb();
    ImGui::TextColored(C_MUTED,"RUNTIME TELEMETRY  /  PROCESS HEALTH");
    ImGui::Separator();
    ImGui::BeginChild("perf-fps",ImVec2(0,88),ImGuiChildFlags_Borders);
    metric("FRAME RATE",std::to_string(static_cast<int>(fps)).c_str(),"FPS",C_TEAL);
    ImGui::SameLine(170); metric("FRAME TIME",std::to_string(s.frameMs).substr(0,5).c_str(),"MS",C_BLUE);
    ImGui::SameLine(340); if(mem>=0) metric("WORKING SET",std::to_string(static_cast<int>(mem)).c_str(),"MB",C_GREEN);
    ImGui::SameLine(510); metric("PARTICLES",std::to_string(s.particles.size()).c_str(),"",C_AMBER);
    ImGui::EndChild();
    ImGui::Spacing(); sectionLabel("FRAME-TIME HISTORY"); ImGui::Spacing();
    ImVec2 a=ImGui::GetContentRegionAvail(); ImVec2 canvas(std::max(1.0f,a.x),std::max(130.0f,a.y));
    ImGui::InvisibleButton("frame-history",canvas); ImVec2 o=ImGui::GetItemRectMin(),z=ImGui::GetItemRectSize(); auto*d=ImGui::GetWindowDrawList();
    d->AddRectFilled(o,{o.x+z.x,o.y+z.y},IM_COL32(7,12,20,255),7);
    for(int i=1;i<5;++i) d->AddLine({o.x,o.y+z.y*i/5},{o.x+z.x,o.y+z.y*i/5},IM_COL32(38,56,73,100));
    for(std::size_t i=1;i<s.frameHistory.size();++i) {
        const float x1=o.x+z.x*float(i-1)/float(s.frameHistory.size()-1), x2=o.x+z.x*float(i)/float(s.frameHistory.size()-1);
        const float y1=o.y+z.y-(std::min(40.0f,s.frameHistory[(s.historyIndex+i-1)%s.frameHistory.size()])/40.0f)*z.y;
        const float y2=o.y+z.y-(std::min(40.0f,s.frameHistory[(s.historyIndex+i)%s.frameHistory.size()])/40.0f)*z.y;
        d->AddLine({x1,y1},{x2,y2},IM_COL32(63,220,219,235),2);
    }
    d->AddText({o.x+12,o.y+10},IM_COL32(145,169,190,220),"FRAME TIME (MS)  /  LAST 90 SAMPLES");
    d->AddRect(o,{o.x+z.x,o.y+z.y},IM_COL32(40,61,80,220),7);
}
void drawInspector(State& s) {
    sectionLabel("EXPERIMENT SETTINGS"); ImGui::Spacing();
    if(s.active==Module::Particles) {
        sectionLabel("SIMULATION"); ImGui::SliderInt("Particle count",&s.particleCount,100,20000);
        ImGui::SliderFloat("Gravity",&s.gravity,0,100,"%.1f");
        ImGui::SliderFloat("Attraction",&s.force,0,500,"%.0f");
        ImGui::SliderFloat("Point size",&s.particleSize,0.5f,3.5f,"%.1f");
        if(ImGui::Button("Apply particle count",ImVec2(-1,0))) resetParticles(s);
        ImGui::Separator(); sectionLabel("VISUALIZATION"); ImGui::Checkbox("Grid overlay",&s.grid); ImGui::Checkbox("Motion trails (preview)",&s.trails);
        ImGui::Separator(); sectionLabel("INTERACTION"); ImGui::TextWrapped("Hold the left mouse button over the viewport to attract particles toward the cursor.");
     } else if(s.active==Module::Renderer) {
        sectionLabel("3D SCENE"); ImGui::Checkbox("Perspective grid",&s.grid); ImGui::Checkbox("Auto orbit",&s.autoRotate);
        ImGui::Checkbox("Star glow",&s.rendererGlow); ImGui::Checkbox("Stellar field",&s.rendererStars);
        ImGui::SliderInt("Star count",&s.particleCount,100,20000);
        ImGui::SliderFloat("Camera zoom",&s.cameraZoom,.35f,3.5f,"%.2fx");
        ImGui::Separator(); sectionLabel("CAMERA"); ImGui::TextWrapped("Drag inside the viewport to orbit the 3D field. Use the mouse wheel to zoom.");
        if(ImGui::Button("Reset camera",ImVec2(-1,0))) { s.cameraZoom=1.0f; s.cameraYaw=0.0f; s.cameraPitch=.12f; }
        ImGui::Separator(); sectionLabel("RENDER PIPELINE"); ImGui::TextColored(C_AMBER,"3D perspective projection"); ImGui::TextDisabled("Procedural CPU particle preview");
    } else if(s.active==Module::Network) {
        sectionLabel("GRAPH NAVIGATION"); ImGui::SliderFloat("Zoom",&s.networkZoom,.28f,4.5f,"%.2fx");
        ImGui::Checkbox("Node movement",&s.networkPhysics); ImGui::Checkbox("IP labels",&s.networkLabels);
        if(ImGui::Button("Reset view",ImVec2(-1,0))) { s.networkZoom=1.0f; s.networkPanX=0.0f; s.networkPanY=0.0f; }
        ImGui::Separator(); sectionLabel("DATA SOURCE"); ImGui::Checkbox("Generate demo traffic",&s.demoTraffic);
        ImGui::Separator(); sectionLabel("CAPTURE STATUS");
        ImGui::TextColored(C_AMBER,"DEMO MODE"); ImGui::TextWrapped("No live interfaces are opened. No packets are captured from the device.");
        ImGui::Separator(); sectionLabel("SESSION TOTALS");
        ImGui::Text("Packets"); ImGui::SameLine(125); ImGui::Text("%llu",static_cast<unsigned long long>(s.packetTotal));
        ImGui::Text("Payload volume"); ImGui::SameLine(125); ImGui::Text("%.1f KB",s.bytesTotal/1024.0);
        if(ImGui::Button("Clear session",ImVec2(-1,0))) { s.packets.clear(); s.packetTotal=0; s.bytesTotal=0; s.packetRate=0; }
    } else {
        sectionLabel("PROCESS"); ImGui::Text("PID"); ImGui::SameLine(125);
#ifdef _WIN32
        ImGui::Text("%lu",static_cast<unsigned long>(GetCurrentProcessId()));
#else
        ImGui::TextDisabled("N/A");
#endif
        ImGui::Text("Memory"); ImGui::SameLine(125); double m=memoryMb(); if(m>=0) ImGui::Text("%.1f MB",m); else ImGui::TextDisabled("N/A");
        ImGui::Separator(); sectionLabel("CAPABILITIES"); ImGui::BulletText("CPU frame timing"); ImGui::BulletText("Process working set");
        ImGui::BulletText("Frame-time history"); ImGui::TextDisabled("GPU telemetry: not implemented");
    }
    ImGui::SetCursorPosY(ImGui::GetWindowHeight()-52);
    ImGui::Separator(); ImGui::TextColored(C_MUTED,"PROFILE"); ImGui::SameLine(); ImGui::TextColored(C_TEAL,"DEFAULT / LOCAL");
}
void drawHeader(State& s) {
    ImGui::BeginChild("Topbar",ImVec2(0,58),ImGuiChildFlags_Borders);
    ImGui::SetCursorPos({15,10});
    ImGui::BeginGroup();
    ImGui::TextColored(C_TEXT,"NEXUS"); ImGui::SameLine(0,2); ImGui::TextColored(C_TEAL,"CORE");
    ImGui::TextColored(C_MUTED,"EXPERIMENTAL SYSTEMS LAB");
    ImGui::EndGroup();
    ImGui::SameLine(250);
    if(ImGui::Button(s.paused?"RESUME":"PAUSE",ImVec2(105,35))) s.paused=!s.paused;
    ImGui::SameLine(); if(ImGui::Button("RESET",ImVec2(90,35))) resetParticles(s);
    ImGui::SameLine(0,22); ImGui::TextColored(C_GREEN,"OK"); ImGui::SameLine(0,5); ImGui::TextColored(C_MUTED,"LOCAL SESSION");
    ImGui::SameLine(ImGui::GetWindowWidth()-120); ImGui::TextColored(C_MUTED,"BUILD"); ImGui::SameLine(0,7); ImGui::TextColored(C_TEAL,"0.2.0");
    ImGui::EndChild();
}
void drawSidebar(State& s) {
    ImGui::BeginChild("Sidebar",ImVec2(210,0),ImGuiChildFlags_Borders);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY()+7); sectionLabel("WORKSPACE"); ImGui::Spacing();
    moduleButton("P","Particle Lab","Dynamics",Module::Particles,s);
    moduleButton("V","Void Renderer","3D viewport",Module::Renderer,s);
    moduleButton("N","NetScope","Traffic lab",Module::Network,s);
    moduleButton("F","Performance","Runtime stats",Module::Performance,s);
    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
    sectionLabel("SYSTEM"); ImGui::Spacing();
    ImGui::BulletText("Graphics"); ImGui::SameLine(); ImGui::TextColored(C_GREEN,"Ready");
    ImGui::BulletText("Simulation"); ImGui::SameLine(); ImGui::TextColored(s.paused?C_AMBER:C_GREEN,s.paused?"Paused":"Running");
    ImGui::BulletText("Network"); ImGui::SameLine(); ImGui::TextColored(C_AMBER,"Demo");
    ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(),ImGui::GetWindowHeight()-48));
    ImGui::Separator(); ImGui::TextColored(C_MUTED,"NEXUS CORE  /  0.2.0");
    ImGui::EndChild();
}
void drawTelemetry(State& s) {
    ImGui::BeginChild("Telemetry",ImVec2(0,91),ImGuiChildFlags_Borders);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY()+5);
    metric("FRAME RATE",std::to_string(static_cast<int>(s.frameMs>0?1000.0/s.frameMs:0)).c_str(),"FPS",C_TEAL);
    ImGui::SameLine(155); metric("FRAME TIME",std::to_string(s.frameMs).substr(0,5).c_str(),"MS",C_BLUE);
    ImGui::SameLine(300); double mem=memoryMb(); if(mem>=0) metric("MEMORY",std::to_string(static_cast<int>(mem)).c_str(),"MB",C_GREEN); else metric("MEMORY","N/A","",C_GREEN);
    ImGui::SameLine(435); metric("PARTICLES",std::to_string(s.particles.size()).c_str(),"",C_AMBER);
    ImGui::SameLine(570); metric("PACKETS",std::to_string(s.packetTotal).c_str(),"",C_BLUE);
    ImGui::SameLine(710); sectionLabel("EVENT LOG"); ImGui::SameLine(); ImGui::TextColored(C_MUTED,s.paused?"Simulation paused":"Runtime loop active");
    ImGui::SameLine(ImGui::GetWindowWidth()-140); ImGui::TextColored(C_GREEN,"ALL SYSTEMS OK");
    ImGui::EndChild();
}
}

int main() {
    if(!glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window=glfwCreateWindow(1500,940,"NEXUS Core | Experimental Systems Lab",nullptr,nullptr);
    if(!window) { glfwTerminate(); return 2; }
    glfwMakeContextCurrent(window); glfwSwapInterval(1);
    IMGUI_CHECKVERSION(); ImGui::CreateContext(); ImGui::GetIO().ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
    style(); ImGui_ImplGlfw_InitForOpenGL(window,true); ImGui_ImplOpenGL3_Init("#version 330");
    State s; resetParticles(s); auto previous=Clock::now();
    while(!glfwWindowShouldClose(window)) {
        glfwPollEvents(); auto now=Clock::now();
        s.frameMs=std::chrono::duration<double,std::milli>(now-previous).count(); previous=now; ++s.frameTotal;
        s.frameHistory[s.historyIndex]=static_cast<float>(s.frameMs); s.historyIndex=(s.historyIndex+1)%s.frameHistory.size();
        if(s.active==Module::Renderer && s.autoRotate && !s.paused) s.rotation+=float(std::min(.05,s.frameMs/1000.0))*.45f;
        ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplGlfw_NewFrame(); ImGui::NewFrame();
        const auto* vp=ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos); ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::Begin("NEXUS_ROOT",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoSavedSettings);
        drawHeader(s);
        ImVec2 remaining=ImGui::GetContentRegionAvail();
        const float telemetryH=98.0f;
        ImGui::BeginChild("MainWorkspace",ImVec2(0,std::max(100.0f,remaining.y-telemetryH)),ImGuiChildFlags_None);
        drawSidebar(s); ImGui::SameLine();
        const float inspectorW=255.0f;
        ImGui::BeginChild("CenterPanel",ImVec2(-inspectorW-12,0),ImGuiChildFlags_Borders);
        const char* title="Particle Lab"; const char* subtitle="Interactive particle dynamics and CPU-side simulation.";
        if(s.active==Module::Renderer){title="Void Renderer";subtitle="Procedural scene preview and rendering experiments.";}
        if(s.active==Module::Network){title="NetScope";subtitle="Synthetic traffic inspection and session metrics.";}
        if(s.active==Module::Performance){title="Performance";subtitle="Runtime health, frame pacing, and process telemetry.";}
        ImGui::Text("%s",title); ImGui::TextColored(C_MUTED,"%s",subtitle); ImGui::Spacing();
        if(s.active==Module::Particles) drawParticles(s);
        else if(s.active==Module::Renderer) drawRenderer(s);
        else if(s.active==Module::Network) drawNetwork(s);
        else drawPerformance(s);
        ImGui::EndChild(); ImGui::SameLine();
        ImGui::BeginChild("Inspector",ImVec2(0,0),ImGuiChildFlags_Borders);
        drawInspector(s);
        ImGui::EndChild();
        ImGui::EndChild();
        drawTelemetry(s);
        ImGui::End();
        ImGui::Render(); int w=0,h=0; glfwGetFramebufferSize(window,&w,&h);
        glViewport(0,0,w,h); glClearColor(C_BG.x,C_BG.y,C_BG.z,1); glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData()); glfwSwapBuffers(window);
    }
    ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplGlfw_Shutdown(); ImGui::DestroyContext();
    glfwDestroyWindow(window); glfwTerminate(); return 0;
}
#ifdef _WIN32
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) { return main(); }
#endif
