#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
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
    ImGui::TextColored(C_MUTED,"VOID ENGINE  /  SCENE PREVIEW");
    ImGui::SameLine(); ImGui::TextColored(C_TEAL,"PROCEDURAL");
    ImGui::Separator();
    ImVec2 a=ImGui::GetContentRegionAvail(), canvas(std::max(1.0f,a.x),std::max(180.0f,a.y));
    ImGui::InvisibleButton("renderer-canvas",canvas);
    ImVec2 o=ImGui::GetItemRectMin(), z=ImGui::GetItemRectSize(); auto*d=ImGui::GetWindowDrawList();
    d->AddRectFilled(o,{o.x+z.x,o.y+z.y},IM_COL32(7,11,19,255),7);
    const float cx=o.x+z.x*.5f, cy=o.y+z.y*.52f;
    if(s.grid) {
        for(int i=-10;i<=10;++i) {
            float x=cx+i*z.x/22;
            d->AddLine({x,o.y+z.y*.20f},{cx+i*z.x/9,o.y+z.y},IM_COL32(34,65,83,100));
        }
        for(int i=0;i<8;++i) {
            float t=float(i+1)/8; d->AddLine({o.x+z.x*(.08f+t*.15f),o.y+z.y*(.55f+t*.45f)},
              {o.x+z.x*(.92f-t*.15f),o.y+z.y*(.55f+t*.45f)},IM_COL32(34,65,83,70));
        }
    }
    const std::array<std::array<float,3>,8> v{{{{-1,-1,-1}},{{1,-1,-1}},{{1,1,-1}},{{-1,1,-1}},{{-1,-1,1}},{{1,-1,1}},{{1,1,1}},{{-1,1,1}}}};
    const float c=std::cos(s.rotation), sn=std::sin(s.rotation); std::array<ImVec2,8> q{};
    for(std::size_t i=0;i<v.size();++i) {
        float x=v[i][0]*c-v[i][2]*sn, zv=v[i][0]*sn+v[i][2]*c+4, y=v[i][1];
        float scale=std::min(z.x,z.y)*.32f/std::max(.7f,zv); q[i]={cx+x*scale*2,cy-y*scale*2};
    }
    constexpr std::array<std::array<int,2>,12> e{{{{0,1}},{{1,2}},{{2,3}},{{3,0}},{{4,5}},{{5,6}},{{6,7}},{{7,4}},{{0,4}},{{1,5}},{{2,6}},{{3,7}}}};
    for(auto edge:e) d->AddLine(q[static_cast<std::size_t>(edge[0])],q[static_cast<std::size_t>(edge[1])],IM_COL32(73,222,231,235),1.8f);
    d->AddCircle({cx,cy},std::min(z.x,z.y)*.23f,IM_COL32(52,135,161,55),64,1);
    d->AddText({o.x+15,o.y+13},IM_COL32(145,169,190,220),"VIEWPORT 01  /  WIREFRAME");
    d->AddText({o.x+15,o.y+z.y-25},IM_COL32(110,140,160,210),"OPENGL CONTEXT ACTIVE  /  MVP GEOMETRY");
    d->AddRect(o,{o.x+z.x,o.y+z.y},IM_COL32(40,61,80,220),7);
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
    ImGui::SameLine(); ImGui::TextColored(C_AMBER,"209 NODES / SYNTHETIC GRAPH");
    ImGui::Separator();
    ImVec2 a=ImGui::GetContentRegionAvail(), canvas(std::max(1.0f,a.x),std::max(180.0f,a.y));
    ImGui::InvisibleButton("network-canvas",canvas,ImGuiButtonFlags_MouseButtonLeft);
    ImVec2 o=ImGui::GetItemRectMin(), z=ImGui::GetItemRectSize(); auto*d=ImGui::GetWindowDrawList();
    d->AddRectFilled(o,{o.x+z.x,o.y+z.y},IM_COL32(12,12,13,255),4);
    for(float x=0;x<z.x;x+=22) d->AddLine({o.x+x,o.y},{o.x+x,o.y+z.y},IM_COL32(45,45,45,38));
    for(float y=0;y<z.y;y+=22) d->AddLine({o.x,o.y+y},{o.x+z.x,o.y+y},IM_COL32(45,45,45,38));
    struct Node { float x,y,r; int type; };
    static std::array<Node,209> nodes{};
    static std::array<std::array<int,2>,627> edges{};
    static bool initialized=false;
    if(!initialized) {
        std::mt19937 rng(0x4E455855u); std::uniform_real_distribution<float> j(-.055f,.055f);
        for(int i=0;i<209;++i) {
            float ang=float(i)*2.39996323f, rad=std::sqrt(float(i)/209.0f);
            nodes[i]={.5f+std::cos(ang)*rad*.43f+j(rng),.5f+std::sin(ang)*rad*.42f+j(rng),1.8f,0};
        }
        for(int i=0;i<209;++i) for(int k=1;k<=3;++k) edges[(i*3+k-1)]={i,(i+k*17+13)%209};
        for(int i=0;i<209;++i) nodes[i].type=(i%31==0)?1:((i%47==0)?2:0);
        initialized=true;
    }
    // Stable deterministic graph, with a small animated pulse on edges.
    for(std::size_t i=0;i<edges.size();++i) {
        auto e=edges[i]; const Node& n1=nodes[e[0]],&n2=nodes[e[1]];
        float x1=o.x+n1.x*z.x,y1=o.y+n1.y*z.y,x2=o.x+n2.x*z.x,y2=o.y+n2.y*z.y;
        int alpha=std::clamp(int(38+18*std::sin(float(ImGui::GetTime())*1.1f+float(i)*.17f)),15,65);
        d->AddLine({x1,y1},{x2,y2},IM_COL32(205,205,205,alpha),.7f);
    }
    const char* hubNames[]={"TCP","Catalyst 9000","ASA 5500","UDP","SIX Series","ICMP","Command Injection","Login Attempt","Data Exfiltration"};
    const ImVec2 hubPos[]={{.50f,.08f},{.17f,.35f},{.82f,.33f},{.08f,.83f},{.51f,.82f},{.90f,.82f},{.50f,.35f},{.40f,.65f},{.63f,.66f}};
    const ImU32 hubColors[]={IM_COL32(255,139,0,255),IM_COL32(43,170,53,255),IM_COL32(43,170,53,255),IM_COL32(255,126,0,255),IM_COL32(43,170,53,255),IM_COL32(255,126,0,255),IM_COL32(222,39,43,255),IM_COL32(222,39,43,255),IM_COL32(222,39,43,255)};
    // Protocol/event hubs connect to every third source node.
    for(int h=0;h<9;++h) {
        ImVec2 hp{o.x+hubPos[h].x*z.x,o.y+hubPos[h].y*z.y};
        for(int i=h*7%209;i<209;i+=3) {
            const Node& n=nodes[i]; ImVec2 np{o.x+n.x*z.x,o.y+n.y*z.y};
            d->AddLine(hp,np,IM_COL32(215,215,215,66),.8f);
        }
    }
    for(int i=0;i<209;++i) {
        ImVec2 p{o.x+nodes[i].x*z.x,o.y+nodes[i].y*z.y};
        d->AddCircleFilled(p,2.8f,IM_COL32(30,133,192,245));
        if(i%7==0) d->AddText({p.x+3,p.y-5},IM_COL32(220,225,230,200),"%03d.%03d.%03d.%03d",(i*17+11)%255,(i*29+43)%255,(i*13+7)%255,(i*31+19)%255);
    }
    for(int h=0;h<9;++h) {
        ImVec2 p{o.x+hubPos[h].x*z.x,o.y+hubPos[h].y*z.y};
        float r=(h==0||h==6||h==7||h==8)?22.0f:19.0f;
        d->AddCircleFilled(p,r+8,IM_COL32((h==0||h==3||h==5)?255:hubColors[h]&255,150,15,20));
        d->AddCircleFilled(p,r,hubColors[h]);
        ImVec2 ts=ImGui::CalcTextSize(hubNames[h]);
        d->AddText({p.x-ts.x*.5f,p.y-ts.y*.5f},IM_COL32(255,255,255,255),hubNames[h]);
    }
    d->AddText({o.x+12,o.y+10},IM_COL32(100,175,220,230),"209 NODES   /   627 CONNECTIONS");
    d->AddText({o.x+12,o.y+z.y-21},IM_COL32(130,130,130,220),"Drag nodes: preview only   |   Synthetic network data");
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
        sectionLabel("SCENE"); ImGui::Checkbox("Perspective grid",&s.grid); ImGui::Checkbox("Auto rotation",&s.autoRotate);
        ImGui::SliderFloat("Rotation",&s.rotation,0,6.28318f,"%.2f rad");
        ImGui::Separator(); sectionLabel("CAMERA"); ImGui::Text("Projection"); ImGui::SameLine(); ImGui::TextColored(C_TEAL,"Perspective");
        ImGui::Text("Geometry"); ImGui::SameLine(); ImGui::TextColored(C_TEAL,"Wireframe cube");
        ImGui::Separator(); sectionLabel("RENDER PIPELINE"); ImGui::TextDisabled("MVP viewport preview"); ImGui::TextDisabled("Shader/material editor: planned");
    } else if(s.active==Module::Network) {
        sectionLabel("DATA SOURCE"); ImGui::Checkbox("Generate demo traffic",&s.demoTraffic);
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
