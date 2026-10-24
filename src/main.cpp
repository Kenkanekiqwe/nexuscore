#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <random>
#include <string>
#include <vector>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#endif

namespace {
using Clock = std::chrono::steady_clock;
enum class Module { Particles, Renderer, Network, Performance };
struct Particle { float x, y, vx, vy; };
struct Packet { double time; std::string src, dst, proto; int bytes; };
struct State {
    Module active = Module::Particles;
    std::vector<Particle> particles;
    std::vector<Packet> packets;
    int particleCount = 1200;
    float gravity = 18.0f, force = 160.0f, rotation = 0.0f;
    bool paused = false, grid = true;
    double frameMs = 0.0, packetRate = 0.0, bytesTotal = 0.0;
    std::uint64_t packetTotal = 0;
    Clock::time_point lastPacket = Clock::now();
};
void resetParticles(State& s) {
    s.particles.clear(); s.particles.reserve(static_cast<std::size_t>(s.particleCount));
    std::mt19937 rng(0x4E455855u);
    std::uniform_real_distribution<float> p(-1.0f, 1.0f), v(-25.0f, 25.0f);
    for (int i=0; i<s.particleCount; ++i) s.particles.push_back({p(rng),p(rng),v(rng),v(rng)});
}
void style() {
    ImGui::StyleColorsDark(); auto& st=ImGui::GetStyle();
    st.WindowRounding=8; st.FrameRounding=5; st.ChildRounding=7; st.WindowPadding={16,14}; st.FramePadding={10,7}; st.ItemSpacing={10,9};
    auto* c=st.Colors;
    c[ImGuiCol_WindowBg]={0.035f,0.047f,0.067f,1}; c[ImGuiCol_ChildBg]={0.045f,0.059f,0.082f,1};
    c[ImGuiCol_Border]={0.13f,0.17f,0.22f,0.8f}; c[ImGuiCol_FrameBg]={0.08f,0.105f,0.14f,1};
    c[ImGuiCol_Button]={0.06f,0.28f,0.33f,1}; c[ImGuiCol_ButtonHovered]={0.08f,0.39f,0.44f,1};
    c[ImGuiCol_Header]={0.06f,0.28f,0.33f,0.75f}; c[ImGuiCol_CheckMark]={0.25f,0.85f,0.85f,1};
    c[ImGuiCol_SliderGrab]={0.10f,0.65f,0.70f,1}; c[ImGuiCol_SliderGrabActive]={0.20f,0.85f,0.85f,1};
}
void nav(const char* label, Module m, State& s) {
    if(s.active==m) ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(0.07f,0.30f,0.35f,1));
    else ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(0.06f,0.08f,0.11f,0));
    if(ImGui::Button(label,{-1,42})) s.active=m;
    ImGui::PopStyleColor();
}
void particles(State& s) {
    ImGui::TextDisabled("PARTICLE LAB / CPU SIMULATION");
    ImGui::SliderInt("Particle count",&s.particleCount,100,20000);
    ImGui::SliderFloat("Gravity",&s.gravity,0,100,"%.1f");
    ImGui::SliderFloat("Cursor attraction",&s.force,0,500,"%.0f");
    if(ImGui::Button("Apply count")) resetParticles(s); ImGui::SameLine();
    if(ImGui::Button(s.paused?"Resume":"Pause")) s.paused=!s.paused; ImGui::SameLine();
    if(ImGui::Button("Reset")) resetParticles(s);
    ImGui::TextDisabled("Hold left mouse over the simulation to attract particles.");
    ImGui::Spacing();
    ImVec2 avail=ImGui::GetContentRegionAvail(); ImVec2 canvas(std::max(1.0f,avail.x),std::max(180.0f,avail.y));
    ImGui::InvisibleButton("particle-canvas",canvas,ImGuiButtonFlags_MouseButtonLeft);
    ImVec2 o=ImGui::GetItemRectMin(), z=ImGui::GetItemRectSize(), mouse=ImGui::GetIO().MousePos;
    auto* d=ImGui::GetWindowDrawList(); d->AddRectFilled(o,{o.x+z.x,o.y+z.y},IM_COL32(10,14,21,255),8);
    if(s.grid) { for(int i=1;i<10;++i){float x=o.x+z.x*i/10;d->AddLine({x,o.y},{x,o.y+z.y},IM_COL32(35,45,58,90));} for(int i=1;i<7;++i){float y=o.y+z.y*i/7;d->AddLine({o.x,y},{o.x+z.x,y},IM_COL32(35,45,58,90));} }
    float dt=std::min(ImGui::GetIO().DeltaTime,0.033f), aspect=z.x/std::max(1.0f,z.y);
    float mx=(mouse.x-o.x)/z.x*2-1, my=1-(mouse.y-o.y)/z.y*2;
    if(!s.paused) for(auto& p:s.particles) {
        p.vy-=s.gravity*dt*0.012f;
        if(ImGui::IsMouseDown(ImGuiMouseButton_Left)){float dx=mx-p.x,dy=my-p.y,den=dx*dx+dy*dy+0.015f;float f=s.force*dt*0.00012f/den;p.vx+=dx*f;p.vy+=dy*f;}
        p.x+=p.vx*dt*0.045f;p.y+=p.vy*dt*0.045f;
        if(p.x < -aspect || p.x > aspect){p.vx*=-0.92f;p.x=std::clamp(p.x,-aspect,aspect);}
        if(p.y < -1 || p.y > 1){p.vy*=-0.92f;p.y=std::clamp(p.y,-1.0f,1.0f);}
        p.vx*=0.9995f;p.vy*=0.9995f;
    }
    for(const auto& p:s.particles){float x=o.x+z.x*(0.5f+p.x/(2*aspect)),y=o.y+z.y*(0.5f-p.y*0.5f);float speed=std::min(1.0f,std::sqrt(p.vx*p.vx+p.vy*p.vy)/50);d->AddCircleFilled({x,y},1.2f+speed,IM_COL32(55,190+int(45*speed),220+int(35*speed),220));}
    d->AddRect(o,{o.x+z.x,o.y+z.y},IM_COL32(55,75,95,180),8);
}
void renderer(State& s) {
    ImGui::TextDisabled("VOID RENDERER / PROCEDURAL WIREFRAME");
    ImGui::Checkbox("Show grid",&s.grid); ImGui::SameLine(); ImGui::SliderFloat("Rotation",&s.rotation,0,6.28318f,"%.2f rad");
    ImVec2 a=ImGui::GetContentRegionAvail(), canvas(std::max(1.0f,a.x),std::max(200.0f,a.y)); ImGui::InvisibleButton("renderer-canvas",canvas);
    ImVec2 o=ImGui::GetItemRectMin(),z=ImGui::GetItemRectSize(); auto*d=ImGui::GetWindowDrawList();
    d->AddRectFilled(o,{o.x+z.x,o.y+z.y},IM_COL32(8,12,20,255),8);
    float cx=o.x+z.x*0.5f,cy=o.y+z.y*0.52f;
    if(s.grid){for(int i=-8;i<=8;++i){float x=cx+i*z.x/18;d->AddLine({x,o.y+z.y*.25f},{cx+i*z.x/8,o.y+z.y},IM_COL32(36,60,78,110));}}
    const std::array<std::array<float,3>,8> v{{{{-1,-1,-1}},{{1,-1,-1}},{{1,1,-1}},{{-1,1,-1}},{{-1,-1,1}},{{1,-1,1}},{{1,1,1}},{{-1,1,1}}}};
    const float c=std::cos(s.rotation),sn=std::sin(s.rotation); std::array<ImVec2,8> q{};
    for(size_t i=0;i<v.size();++i){float x=v[i][0]*c-v[i][2]*sn,zv=v[i][0]*sn+v[i][2]*c+4,y=v[i][1],scale=std::min(z.x,z.y)*.34f/std::max(.7f,zv);q[i]={cx+x*scale*2,cy-y*scale*2};}
    constexpr std::array<std::array<int,2>,12> e{{{{0,1}},{{1,2}},{{2,3}},{{3,0}},{{4,5}},{{5,6}},{{6,7}},{{7,4}},{{0,4}},{{1,5}},{{2,6}},{{3,7}}}};
    for(auto edge:e)d->AddLine(q[static_cast<size_t>(edge[0])],q[static_cast<size_t>(edge[1])],IM_COL32(75,220,235,230),1.7f);
    d->AddText({o.x+16,o.y+14},IM_COL32(140,170,190,220),"PROCEDURAL SCENE / WIREFRAME MVP");d->AddRect(o,{o.x+z.x,o.y+z.y},IM_COL32(55,75,95,180),8);
}
void synthesize(State& s) {
    auto now=Clock::now();double elapsed=std::chrono::duration<double>(now-s.lastPacket).count();if(elapsed<.12)return;s.lastPacket=now;
    static std::mt19937 rng(0x4E455453u);static const std::array<const char*,4> protos{"TCP","UDP","DNS","TLS"};
    std::uniform_int_distribution<int> ip(2,250),pr(0,3),sz(64,1450),npack(1,5);int n=npack(rng);
    for(int i=0;i<n;++i){Packet p{ImGui::GetTime(),"192.168.1."+std::to_string(ip(rng)),"10.0.0."+std::to_string(ip(rng)),protos[static_cast<size_t>(pr(rng))],sz(rng)};s.bytesTotal+=p.bytes;++s.packetTotal;s.packets.push_back(std::move(p));}
    while(s.packets.size()>250)s.packets.erase(s.packets.begin());s.packetRate=n/elapsed;
}
void network(State& s) {
    synthesize(s);ImGui::TextDisabled("NETSCOPE / SYNTHETIC TRAFFIC");ImGui::SameLine();ImGui::TextColored({1,.72f,.25f,1},"DEMO MODE — NO LIVE CAPTURE");
    ImGui::TextWrapped("Sample rows only: this MVP does not capture, transmit, or inspect real network traffic.");
    ImGui::Text("Packets: %llu  |  Rate: %.1f pkt/s  |  Data: %.1f KB",static_cast<unsigned long long>(s.packetTotal),s.packetRate,s.bytesTotal/1024.0);
    if(ImGui::BeginTable("packets",5,ImGuiTableFlags_BordersInnerH|ImGuiTableFlags_RowBg|ImGuiTableFlags_ScrollY|ImGuiTableFlags_Resizable,{0,-1})){
        ImGui::TableSetupColumn("Time");ImGui::TableSetupColumn("Source");ImGui::TableSetupColumn("Destination");ImGui::TableSetupColumn("Protocol");ImGui::TableSetupColumn("Bytes");ImGui::TableHeadersRow();
        for(auto it=s.packets.rbegin();it!=s.packets.rend();++it){ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::Text("%.2f",it->time);ImGui::TableNextColumn();ImGui::TextUnformatted(it->src.c_str());ImGui::TableNextColumn();ImGui::TextUnformatted(it->dst.c_str());ImGui::TableNextColumn();ImGui::TextUnformatted(it->proto.c_str());ImGui::TableNextColumn();ImGui::Text("%d",it->bytes);}
        ImGui::EndTable();
    }
}
double memoryMb(){
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS c{};c.cb=sizeof(c);if(GetProcessMemoryInfo(GetCurrentProcess(),&c,sizeof(c)))return double(c.WorkingSetSize)/(1024.0*1024.0);
#endif
    return -1;
}
void performance(State& s) {
    ImGui::TextDisabled("RUNTIME TELEMETRY");
    ImGui::Text("Frame time");ImGui::SameLine(220);ImGui::Text("%.3f ms",s.frameMs);
    ImGui::Text("Frame rate");ImGui::SameLine(220);ImGui::Text("%.1f FPS",s.frameMs>0?1000/s.frameMs:0);
    ImGui::Text("Process working set");ImGui::SameLine(220);double m=memoryMb();if(m>=0)ImGui::Text("%.1f MB",m);else ImGui::TextDisabled("Unavailable");
    ImGui::Text("Particles");ImGui::SameLine(220);ImGui::Text("%zu",s.particles.size());
    ImGui::Text("Retained packet rows");ImGui::SameLine(220);ImGui::Text("%zu",s.packets.size());
    ImGui::Separator();ImGui::TextWrapped("CPU frame timing only. GPU timers and hardware utilization counters are not implemented yet.");
}
}

int main() {
    if(!glfwInit())return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window=glfwCreateWindow(1440,900,"NEXUS Core | Experimental Systems Lab",nullptr,nullptr);
    if(!window){glfwTerminate();return 2;}glfwMakeContextCurrent(window);glfwSwapInterval(1);
    IMGUI_CHECKVERSION();ImGui::CreateContext();ImGui::GetIO().ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;style();
    ImGui_ImplGlfw_InitForOpenGL(window,true);ImGui_ImplOpenGL3_Init("#version 330");
    State s;resetParticles(s);auto previous=Clock::now();
    while(!glfwWindowShouldClose(window)){
        glfwPollEvents();auto now=Clock::now();s.frameMs=std::chrono::duration<double,std::milli>(now-previous).count();previous=now;s.rotation+=float(std::min(.05,s.frameMs/1000.0))*.45f;
        ImGui_ImplOpenGL3_NewFrame();ImGui_ImplGlfw_NewFrame();ImGui::NewFrame();
        const auto* vp=ImGui::GetMainViewport();ImGui::SetNextWindowPos(vp->WorkPos);ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::Begin("NEXUS_ROOT",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoSavedSettings);
        ImGui::BeginChild("Sidebar",{224,0},ImGuiChildFlags_Borders);
        ImGui::TextColored({.25f,.85f,.88f,1},"N E X U S");ImGui::TextDisabled("CORE / SYSTEMS LAB");ImGui::Spacing();ImGui::Separator();ImGui::Spacing();
        nav("  Particle Lab",Module::Particles,s);nav("  Void Renderer",Module::Renderer,s);nav("  NetScope",Module::Network,s);nav("  Performance",Module::Performance,s);
        ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(),ImGui::GetWindowHeight()-70));ImGui::Separator();ImGui::TextDisabled("BUILD 0.1.0 / MVP");ImGui::TextColored({.35f,.8f,.62f,1},"● SYSTEM ONLINE");ImGui::EndChild();ImGui::SameLine();
        ImGui::BeginChild("MainPanel",{0,0});
        const char* title="Particle Lab";const char* subtitle="Interactive particle dynamics and CPU-side simulation.";
        if(s.active==Module::Renderer){title="Void Renderer";subtitle="Procedural scene preview and rendering experiments.";}
        if(s.active==Module::Network){title="NetScope";subtitle="Packet-flow metrics in synthetic demo mode.";}
        if(s.active==Module::Performance){title="Performance";subtitle="Runtime metrics for the current process.";}
        ImGui::Text("%s",title);ImGui::TextDisabled("%s",subtitle);ImGui::Separator();
        if(s.active==Module::Particles)particles(s);else if(s.active==Module::Renderer)renderer(s);else if(s.active==Module::Network)network(s);else performance(s);
        ImGui::EndChild();ImGui::End();ImGui::Render();
        int w=0,h=0;glfwGetFramebufferSize(window,&w,&h);glViewport(0,0,w,h);glClearColor(.025f,.033f,.047f,1);glClear(GL_COLOR_BUFFER_BIT);ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());glfwSwapBuffers(window);
    }
    ImGui_ImplOpenGL3_Shutdown();ImGui_ImplGlfw_Shutdown();ImGui::DestroyContext();glfwDestroyWindow(window);glfwTerminate();return 0;
}
