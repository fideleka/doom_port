// Synthetic authored fixtures only: no WAD or GENMIDI assets.
#include <chrono>
static std::vector<uint8_t> syntheticBank() {
    std::vector<uint8_t> b(8+175*36); memcpy(b.data(),"#OPL_II#",8);
    for(int i=0;i<175;i++) for(int j=0;j<2;j++) {
        auto* p=b.data()+8+i*36+4+j*16;
        p[0]=1;p[1]=0xf2;p[2]=0x74;p[3]=0;p[4]=0;p[5]=20;p[6]=0;
        p[7]=1;p[8]=0xf2;p[9]=0x74;p[10]=0;p[11]=0;p[12]=0;
    }
    return b;
}
static std::vector<uint8_t> syntheticMus(std::vector<uint8_t> events) {
    std::vector<uint8_t> s(16); memcpy(s.data(),"MUS\x1a",4);
    s[4]=events.size()&255; s[5]=events.size()>>8;s[6]=16;s[8]=1;
    s.insert(s.end(),events.begin(),events.end());return s;
}
static std::vector<int32_t> renderMusic(doom_audio::MusOPL& m,size_t n) {
    std::vector<int32_t> out(n);m.renderAdd(out.data(),out.size());return out;
}
static bool nonzero(const std::vector<int32_t>& v) { for(auto x:v)if(x)return true;return false; }
static void musicTests() {
    using namespace doom_audio;
    auto bank=syntheticBank(); MusOPL m;assert(!m.init(nullptr,0));assert(m.init(bank.data(),bank.size()));
    auto bytes=syntheticMus({0x90,0x80|60,127,0x81,0x0c,0x60}); // 140 ticks = exact second
    Song song;assert(Song::parse(bytes.data(),bytes.size(),song));
    for(size_t n=0;n<bytes.size();n++){Song bad;assert(!Song::parse(bytes.data(),n,bad));}
    m.play(&song,false);auto pcm=renderMusic(m,kRate);assert(nonzero(pcm)&&m.isPlaying());
    renderMusic(m,1);assert(!m.isPlaying());
    m.play(&song,true);renderMusic(m,kRate*3);assert(m.isPlaying());
    m.pause(true);auto paused=renderMusic(m,400);assert(!nonzero(paused)&&m.isPlaying());m.pause(false);assert(nonzero(renderMusic(m,400)));
    m.setVolume(0);assert(!nonzero(renderMusic(m,400)));m.setVolume(127);assert(nonzero(renderMusic(m,400)));m.stop();assert(!m.isPlaying());
    MusOPL a,b;assert(a.init(bank.data(),bank.size())&&b.init(bank.data(),bank.size()));a.play(&song,false);b.play(&song,false);b.setVolume(32);
    auto full=renderMusic(a,1000), quiet=renderMusic(b,1000);for(size_t i=0;i<full.size();i++)assert(quiet[i]==full[i]*32/127);
    // Pause freezes envelopes AND sample-clock; resumed sequence equals uninterrupted one.
    a.play(&song,false);b.setVolume(127);b.play(&song,false);
    assert(renderMusic(a,300)==renderMusic(b,300));b.pause(true);renderMusic(b,999);b.pause(false);assert(renderMusic(a,500)==renderMusic(b,500));
    std::vector<uint8_t> pressure;
    for(int i=0;i<20;i++){pressure.push_back(0x10);pressure.push_back(0x80|uint8_t(40+i));pressure.push_back(100);}
    pressure.insert(pressure.end(),{0x2f,220,0x9f,0x80|35,110,20,0x00,40,0x40,8,127,0x90,60,5,0x40,8,0,0x60});
    auto busy=syntheticMus(pressure);Song ps;assert(Song::parse(busy.data(),busy.size(),ps));
    bank[8]|=4;assert(m.init(bank.data(),bank.size()));m.play(&ps,false);assert(nonzero(renderMusic(m,1000)));
    auto bent=syntheticMus({0x20,255,0x90,0x80|60,127,20,0x60});Song bs;assert(Song::parse(bent.data(),bent.size(),bs));
    a.init(bank.data(),bank.size());b.init(bank.data(),bank.size());a.play(&song,false);b.play(&bs,false);assert(renderMusic(a,500)!=renderMusic(b,500));
    for(auto ev:std::vector<std::vector<uint8_t>>{{0x10},{0x10,128},{0x90,60,128,128,128,128},{0x50},{0x70},{0x30,255},{0x40,3,255},{0x10,60,0x60}}) {
        auto bad=syntheticMus(ev);Song s;assert(Song::parse(bad.data(),bad.size(),s));m.play(&s,true);renderMusic(m,10);assert(!m.isPlaying());
    }
    // Short variable deltas retain fractional clock: 2 ticks -> ceil(11025*2/140)=158 samples.
    auto shortBytes=syntheticMus({0x90,0x80|60,127,1,0x90,64,1,0x60});Song shortSong;
    assert(Song::parse(shortBytes.data(),shortBytes.size(),shortSong));m.play(&shortSong,false);
    renderMusic(m,157);assert(m.isPlaying());renderMusic(m,1);assert(m.isPlaying());renderMusic(m,1);assert(!m.isPlaying());
    // GENMIDI changes actually reach FM operators, not merely an instrument label.
    auto altered=syntheticBank();altered[8+4+7]=7;altered[8+4+10]=1;
    a.init(bank.data(),bank.size());b.init(altered.data(),altered.size());a.play(&song,false);b.play(&song,false);
    assert(renderMusic(a,1000)!=renderMusic(b,1000));
    // Deterministic malformed-score and hostile GENMIDI fuzz: bounded reads/events under sanitizers.
    uint32_t random=0x12345678;
    auto next=[&](){random=random*1664525u+1013904223u;return uint8_t(random>>24);};
    for(int test=0;test<200;test++) {
        auto hostile=syntheticBank();for(size_t i=8;i<hostile.size();i++)hostile[i]=next();
        assert(m.init(hostile.data(),hostile.size()));
        std::vector<uint8_t> ev(1+next());for(auto& x:ev)x=next();
        auto fuzz=syntheticMus(ev);Song fs;assert(Song::parse(fuzz.data(),fuzz.size(),fs));m.play(&fs,test%2);renderMusic(m,300);
    }
    // Music added to wide effect sum: neither subsystem replaces/mutes the other.
    uint8_t sfx[1000];memset(sfx,129,sizeof(sfx));Mixer fx;fx.start(0,{sfx,1000,kRate},127,0);
    a.init(bank.data(),bank.size());b.init(bank.data(),bank.size());a.play(&song,false);b.play(&song,false);
    int32_t combined[500];fx.render(combined,500);a.renderAdd(combined,500);auto only=renderMusic(b,500);
    for(int i=0;i<500;i++)assert(combined[i]==only[i]+256);
    int16_t muted[500];Mixer::output(combined,muted,500,0);for(auto x:muted)assert(x==0);
    // Registration copies score and unregister detaches under same lock.
    lumps[0]=syntheticBank();selected=0;
    writer=[](const void*,size_t size,size_t* written){*written=size;std::this_thread::yield();return ESP_OK;};
    DG_sound_module.Init=nullptr;assert(!MusicInit());DG_sound_module=sound_module_I2S;
    int before=installs;assert(I_I2S_InitSound(true));assert(MusicInit());assert(installs==before+1);
    void* h=MusicRegister(bytes.data(),bytes.size());assert(h);assert(!MusicRegister(bytes.data(),bytes.size()));
    assert(songStorage!=song.bytes);MusicPlay(h,true);assert(MusicPlaying());MusicPause();assert(MusicPlaying());MusicResume();
    MusicUnregister(h);assert(!MusicPlaying()&&!songStorage);MusicUnregister(h);
    // Stop normal task before join; shutdown waits its exit and releases exactly once.
    MusicShutdown();assert(running.load() && installed); // music shutdown must not suppress SFX
    running.store(false);audioThread.join();I_I2S_ShutdownSound();assert(installs==uninstalls&&semaphoreCount==0);
    assert(MusicInit() && musicOwnsOutput); // music-only (-nosfx) lifecycle
    running.store(false);audioThread.join();MusicShutdown();assert(installs==uninstalls&&semaphoreCount==0);
    auto start=std::chrono::steady_clock::now();m.init(bank.data(),bank.size());m.play(&ps,true);renderMusic(m,kRate*10);
    auto us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count();
    printf("music: real FM, 140Hz variable delta, loop/pause/gains/mute, pressure/doublevoice/percussion/bend, malformed, copied registration, simultaneous SFX/single driver PASS; MusOPL=%zu bytes; host 10s=%lldus (current host test mode, not ESP32)\n",sizeof(MusOPL),(long long)us);
}
