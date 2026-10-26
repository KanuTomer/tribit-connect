#pragma once
#include "speaker.hpp"
#include <winrt/base.h>
#include <algorithm>
#include <array>
#include <memory>
#include <mutex>
#include <thread>

struct Snapshot {
    bool ready{}, pending{}, fresh{};
    std::string status="Not connected", error, firmware, address;
    int battery=-1;
    int preset=-1;                         // tribit::Preset id, or -1 when not read
    std::array<int,tribit::kEqBands> bands{}; // dB, only meaningful while preset is Custom
    std::array<int,5> prompts{-1,-1,-1,-1,-1};
    int auto_shutdown=-1;
    std::array<int,3> buttons{-1,-1,-1};   // preset id per EQ button light (Custom: see button_bands)
    std::array<std::array<int,tribit::kEqBands>,3> button_bands{}; // dB, for lights holding a custom curve
    int64_t checked{};
    bool release_when_hidden{};
};

// The six presets the speaker accepts, in the order the Tribit app lists them.
inline constexpr std::array<uint8_t,6> kPresetOrder{tribit::XBassOff,tribit::XBass,tribit::MaxSound,tribit::Rock,tribit::Audiobook,tribit::Jazz};
inline constexpr std::array<const char*,5> kPromptLabels{"Power on/off beep","Bluetooth pairing beep","Low battery beep","Maximum volume beep","TWS pairing beep"};
inline constexpr std::array<const char*,3> kLightLabels{"Light off","White light","Blue light"};
inline std::string preset_label(int preset){return preset<0?"Not checked":tribit::preset_name(static_cast<uint8_t>(preset));}
inline bool valid_preset(int preset){return std::find(kPresetOrder.begin(),kPresetOrder.end(),preset)!=kPresetOrder.end();}

enum class Operation { Activate, Deactivate, Retry, Preset, CustomEq, Prompt, AutoShutdown, Buttons, Policy, Suspend, Reconnect, Disconnect };
using Bands=std::array<int,tribit::kEqBands>;
// `id` (non-zero) asks for a result callback once the speaker confirms or the command fails.
struct Command { Operation operation; int value{}; int index{}; std::array<int,3> buttons{}; int id{}; Bands bands{}; std::array<Bands,3> button_bands{}; };

class Controller {
public:
    explicit Controller(std::function<void()> notify, Snapshot initial={}) : state_(std::move(initial)), notify_(std::move(notify)), worker_([this]{run();}) {}
    ~Controller() { {std::lock_guard lock(mutex_); stopping_=true;} SetEvent(wake_); worker_.join();CloseHandle(wake_); }
    Snapshot snapshot() const { std::lock_guard lock(mutex_); return state_; }
    void interest(bool active) {
        std::lock_guard lock(mutex_);
        if (requested_active_==active) return;
        requested_active_=active;
        commands_.push_back({active?Operation::Activate:Operation::Deactivate});
        SetEvent(wake_);
    }
    void lifecycle(Operation operation){std::lock_guard lock(mutex_);if(std::none_of(commands_.begin(),commands_.end(),[&](auto& c){return c.operation==operation;}))commands_.push_back({operation});SetEvent(wake_);}
    void release_when_hidden(bool release){
        {std::lock_guard lock(mutex_);state_.release_when_hidden=release;commands_.push_back({Operation::Policy});SetEvent(wake_);}
        notify_();
    }
    // Called on the worker thread with a command's id once it is confirmed or has failed.
    std::function<void(int,bool,const std::string&)> on_result;
    bool send(Command command) {
        {
            std::lock_guard lock(mutex_);
            if(command.operation==Operation::Retry && (!requested_active_||state_.pending)) return false;
            if(command.operation!=Operation::Retry && (!requested_active_ || !state_.ready || !state_.fresh||state_.pending)) return false;
            if(commands_.size()>=16) return false;
            commands_.push_back(std::move(command));
            state_.pending=true;
        }
        notify_();
        SetEvent(wake_);return true;
    }
private:
    mutable std::mutex mutex_;
    static HANDLE make_wake(){auto event=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!event)winrt::throw_last_error();return event;}
    HANDLE wake_=make_wake();
    Snapshot state_;
    std::function<void()> notify_;
    std::deque<Command> commands_;
    bool requested_active_{}, stopping_{};
    std::thread worker_;

    void report(int id,bool ok,const std::string& error={}){if(id&&on_result)on_result(id,ok,error);}
    // Drops queued setting commands (the link is gone) and fails the ones that asked for a result.
    void drop_settings(const std::string& why){
        std::vector<int> failed;
        {std::lock_guard lock(mutex_);std::erase_if(commands_,[&](const auto& c){bool drop=c.operation!=Operation::Activate&&c.operation!=Operation::Deactivate;if(drop&&c.id)failed.push_back(c.id);return drop;});}
        for(int id:failed)report(id,false,why);
    }
    void publish(const std::function<void(Snapshot&)>& change) {
        {std::lock_guard lock(mutex_); change(state_);}
        notify_();
    }
    static std::optional<tribit::Bytes> get(tribit::Speaker& speaker,uint8_t command){
        auto replies=speaker.request(command);
        if(replies.empty())return std::nullopt;
        return replies.front().data;
    }
    // Field readers; each fills one part of a snapshot from the speaker.
    static void read_eq(tribit::Speaker& speaker,Snapshot& next){
        using namespace tribit;
        auto eq_data=get(speaker,GetEq);auto eq=eq_data?parse_eq(*eq_data):std::nullopt;
        if(!eq)throw std::runtime_error("The speaker did not report its equalizer.");
        next.preset=eq->preset;
        for(size_t i=0;i<kEqBands;++i)next.bands[i]=band_db(eq->bands[i]);
    }
    static void read_prompts(tribit::Speaker& speaker,Snapshot& next){
        auto data=get(speaker,tribit::GetVoicePrompts);
        if(auto prompts=data?tribit::parse_voice_prompts(*data):std::nullopt)for(size_t i=0;i<prompts->size();++i)next.prompts[i]=(*prompts)[i]?1:0;
        else next.prompts.fill(-1);
    }
    static void read_shutdown(tribit::Speaker& speaker,Snapshot& next){
        auto data=get(speaker,tribit::GetAutoShutdown);
        next.auto_shutdown=data&&data->size()==3&&(*data)[2]<=1?(*data)[2]:-1;
    }
    static void read_buttons(tribit::Speaker& speaker,Snapshot& next){
        next.buttons.fill(-1);
        for(auto& frame:speaker.request(tribit::GetEqButtons,{},3))
            if(auto slot=tribit::parse_button_slot(frame.data)){
                next.buttons[slot->light]=slot->preset;
                for(size_t i=0;i<tribit::kEqBands;++i)next.button_bands[slot->light][i]=slot->preset==tribit::Custom?tribit::band_db(frame.data[3+i]):0;
            }
    }
    // Custom curves: -8..+8 dB per band. The speaker stores anything, so the limit is ours.
    static tribit::Eq custom_eq(const Bands& bands){
        tribit::Eq eq{tribit::Custom};
        for(size_t i=0;i<bands.size();++i){if(bands[i]<-8||bands[i]>8)throw std::runtime_error("Band gains must be between -8 and +8 dB.");eq.bands[i]=static_cast<uint8_t>(bands[i]+tribit::kBandZero);}
        return eq;
    }
    // Publishes a confirmed snapshot together with the queue state, so the widget gets one update, not three.
    void commit(Snapshot next){
        next.ready=true;next.fresh=true;next.error.clear();next.status="Connected";
        next.checked=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        publish([&](Snapshot& state){next.pending=!commands_.empty();next.release_when_hidden=state.release_when_hidden;state=std::move(next);});
    }
    void read(tribit::Speaker& speaker) {
        using namespace tribit;
        auto next=snapshot();
        auto info=get(speaker,GetDeviceInfo);
        if(!info)throw std::runtime_error("The speaker did not answer. Turn it off and on, then retry.");
        if(info->size()>=6)next.firmware=std::to_string((*info)[3])+"."+std::to_string((*info)[4])+"."+std::to_string((*info)[5]);
        read_eq(speaker,next);
        auto battery=get(speaker,GetBattery);
        next.battery=battery&&battery->size()==1&&(*battery)[0]<=100?(*battery)[0]:-1;
        read_prompts(speaker,next);read_shutdown(speaker,next);read_buttons(speaker,next);
        commit(std::move(next));
    }
    static void acknowledged(const std::vector<tribit::Frame>& replies,const char* what){
        if(replies.empty())throw std::runtime_error(std::string(what)+" was not acknowledged by the speaker.");
    }
    void execute(tribit::Speaker& speaker,const Command& command) {
        using namespace tribit;
        auto next=snapshot();
        bool confirmed=false;
        // Each change reads back only the setting it touched; a full read costs about half a second.
        if(command.operation==Operation::Preset){
            if(!valid_preset(command.value))throw std::runtime_error("Unknown EQ preset.");
            acknowledged(speaker.request(SetEq,eq_payload({static_cast<uint8_t>(command.value)})),"The preset change");
            read_eq(speaker,next);confirmed=next.preset==command.value;
        }else if(command.operation==Operation::CustomEq){
            acknowledged(speaker.request(SetEq,eq_payload(custom_eq(command.bands))),"The custom EQ");
            read_eq(speaker,next);confirmed=next.preset==Custom&&next.bands==command.bands;
        }else if(command.operation==Operation::Prompt){
            if(command.index<0||command.index>4||std::any_of(next.prompts.begin(),next.prompts.end(),[](int v){return v<0;}))throw std::runtime_error("Voice prompts have not been read.");
            VoicePrompts prompts{};for(size_t i=0;i<prompts.size();++i)prompts[i]=next.prompts[i]==1;
            prompts[command.index]=command.value!=0;
            acknowledged(speaker.request(SetVoicePrompts,voice_prompts_payload(prompts)),"The voice prompt change");
            read_prompts(speaker,next);confirmed=next.prompts[command.index]==(command.value?1:0);
        }else if(command.operation==Operation::AutoShutdown){
            acknowledged(speaker.request(SetAutoShutdown,Bytes{static_cast<uint8_t>(command.value?1:0)}),"The auto shutdown change");
            read_shutdown(speaker,next);confirmed=next.auto_shutdown==(command.value?1:0);
        }else if(command.operation==Operation::Buttons){
            // Each light holds a factory preset or its own custom curve.
            std::array<Eq,3> slots{};
            for(size_t i=0;i<slots.size();++i){
                if(command.buttons[i]==Custom)slots[i]=custom_eq(command.button_bands[i]);
                else if(valid_preset(command.buttons[i]))slots[i]=Eq{static_cast<uint8_t>(command.buttons[i])};
                else throw std::runtime_error("Unknown EQ preset.");
            }
            speaker.send(eq_slots_write(slots));
            acknowledged(speaker.collect(SetEqButtons,3,1500ms),"The EQ button change");
            read_buttons(speaker,next);confirmed=next.buttons==command.buttons;
            for(size_t i=0;i<slots.size();++i)if(command.buttons[i]==Custom&&next.button_bands[i]!=command.button_bands[i])confirmed=false;
        }else throw std::runtime_error("Unsupported setting command.");
        // A change only counts once the speaker reports it back.
        if(!confirmed)throw std::runtime_error("The speaker did not confirm the change.");
        commit(std::move(next));
    }
    void run() {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        std::unique_ptr<tribit::Speaker> speaker;
        bool active=false,suspended=false;
        auto release_at=Clock::time_point::max(),refresh_at=Clock::time_point::max();
        // A speaker often accepts the audio link before its control channel is ready,
        // so a failed connect retries on its own with backoff.
        constexpr std::chrono::seconds retry_delays[]={2s,5s,10s,20s,30s,60s};
        auto retry_at=Clock::time_point::max();size_t retries=0;
        int link_grace=0; // rechecks after a connect event, before Windows marks the pair connected
        for(;;) {
            std::optional<Command> command;
            {
                std::unique_lock lock(mutex_);
                if(stopping_) break;
                if(!commands_.empty()){command=std::move(commands_.front());commands_.pop_front();}
                if(commands_.empty())ResetEvent(wake_);
            }
            bool scheduled=false,connecting=false;
            if(!command&&!speaker&&Clock::now()>=retry_at){scheduled=true;retry_at=Clock::time_point::max();command=Command{active?Operation::Retry:Operation::Reconnect};}
            // A fresh trigger (activation, Bluetooth link, resume) restarts the backoff.
            if(command&&!scheduled&&(command->operation==Operation::Activate||command->operation==Operation::Retry||command->operation==Operation::Reconnect)){retries=0;retry_at=Clock::time_point::max();link_grace=command->operation==Operation::Reconnect?2:0;}
            try {
                if(command && command->operation==Operation::Deactivate) {
                    active=false;refresh_at=Clock::time_point::max();
                    release_at=snapshot().release_when_hidden?Clock::now()+15s:Clock::time_point::max();
                    if(!speaker) publish([](auto& s){s.ready=false;s.pending=false;s.fresh=false;s.status="Not connected";});
                } else if(command&&command->operation==Operation::Policy){release_at=!active&&snapshot().release_when_hidden?Clock::now()+15s:Clock::time_point::max();
                } else if(command&&(command->operation==Operation::Suspend||command->operation==Operation::Disconnect)){
                    suspended=command->operation==Operation::Suspend;speaker.reset();release_at=refresh_at=retry_at=Clock::time_point::max();retries=0;
                    publish([&](auto& s){s.ready=s.pending=s.fresh=false;s.status=suspended?"Sleeping":"Not connected";s.error.clear();});
                } else if(command && (command->operation==Operation::Activate || command->operation==Operation::Retry||command->operation==Operation::Reconnect)) {
                    if(command->operation==Operation::Reconnect){suspended=false;if(snapshot().address.empty()||(!active&&snapshot().release_when_hidden))continue;}
                    else {active=true;release_at=Clock::time_point::max();}
                    if(suspended)continue;
                    if(!speaker) {
                        connecting=true;
                        auto previous=tribit::parse_address(snapshot().address);
                        auto device=tribit::find_speaker(previous);
                        if(!device.connected)throw tribit::NotConnected("Speaker not connected.");
                        publish([](auto& s){s.ready=false;s.fresh=false;s.pending=true;s.status="Connecting...";s.error.clear();});
                        speaker=std::make_unique<tribit::Speaker>();speaker->connect(device.address);
                        publish([&](auto& s){s.address=tribit::format_address(device.address);});
                        read(*speaker);retries=0;
                    }else if(command->operation!=Operation::Reconnect){
                        publish([](auto& s){s.pending=true;s.status="Refreshing...";});
                        read(*speaker);
                    }
                } else if(command) {
                    if(!speaker || !active) throw std::runtime_error("Controls are inactive. Open the widget and retry.");
                    execute(*speaker,*command);
                    report(command->id,true);
                }
                if(speaker && !active && Clock::now()>=release_at) {
                    speaker.reset();
                    publish([](auto& s){s.ready=false;s.fresh=false;s.status="Controls idle";});
                }
                if(speaker){
                    // The frame format of the speaker's own notifications is not decoded yet;
                    // any of them means something changed, so read everything again shortly.
                    while(auto frame=speaker->receive(Clock::now()))speaker->unsolicited.push_back(std::move(*frame));
                    if(!speaker->unsolicited.empty()){speaker->unsolicited.clear();refresh_at=std::min(refresh_at,Clock::now()+2s);}
                    // Battery has no notification we know of; keep it current while a widget is visible.
                    if(active&&refresh_at==Clock::time_point::max())refresh_at=Clock::now()+60s;
                    if(Clock::now()>=refresh_at){refresh_at=active?Clock::now()+60s:Clock::time_point::max();read(*speaker);}
                }
                bool pending;
                {std::lock_guard lock(mutex_);pending=!commands_.empty();}
                if(snapshot().pending!=pending) publish([&](auto& s){s.pending=pending;});
            } catch(const tribit::NotConnected&) {
                // The Bluetooth connect event wakes us with Reconnect; no polling until then.
                // A dropped control channel gets one recheck, in case the speaker is still on.
                if(speaker&&link_grace==0)link_grace=1;
                speaker.reset();refresh_at=Clock::time_point::max();
                if(command)report(command->id,false,"The speaker is not connected.");
                drop_settings("The speaker is not connected.");
                if(link_grace>0&&retries<std::size(retry_delays)){--link_grace;retry_at=Clock::now()+retry_delays[retries++];}
                else{retry_at=Clock::time_point::max();retries=0;link_grace=0;}
                publish([](auto& s){s.ready=false;s.pending=false;s.fresh=false;s.status="Not connected";s.error.clear();});
            } catch(const std::exception& error) {
                speaker.reset();refresh_at=Clock::time_point::max();
                if(command)report(command->id,false,error.what());
                drop_settings("The speaker connection was reset.");
                publish([&](auto& s){s.ready=false;s.pending=false;s.fresh=false;s.status="Controls unavailable";s.error=error.what();});
                if(connecting&&!suspended&&retries<std::size(retry_delays))retry_at=Clock::now()+retry_delays[retries++];
            } catch(const winrt::hresult_error& error) {
                speaker.reset();refresh_at=Clock::time_point::max();
                if(command)report(command->id,false,winrt::to_string(error.message()));
                drop_settings("Bluetooth is unavailable.");
                if(connecting&&!suspended&&retries<std::size(retry_delays))retry_at=Clock::now()+retry_delays[retries++];
                publish([&](auto& s){s.ready=false;s.pending=false;s.fresh=false;s.status="Bluetooth unavailable";s.error=winrt::to_string(error.message());});
            }
            // No scheduled work while idle. Only socket data, commands, lifecycle events,
            // the visible-widget refresh or the optional release deadline wake us.
            HANDLE waits[]={wake_,speaker?speaker->wait_handle():nullptr};
            DWORD timeout=INFINITE;
            auto deadline=speaker?std::min(release_at,refresh_at):retry_at;
            if(deadline!=Clock::time_point::max())timeout=static_cast<DWORD>(std::clamp<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline-Clock::now()).count(),0,MAXDWORD-1));
            auto waited=WaitForMultipleObjects(waits[1]?2:1,waits,FALSE,timeout);
            if(waited==WAIT_FAILED){publish([](auto& s){s.ready=s.pending=s.fresh=false;s.status="Controls unavailable";s.error="Could not wait for Bluetooth events.";});break;}
        }
    }
};
