#include <iostream>
#include <random>
#include <koisyn.h>

using namespace std;
using namespace std::chrono;
using namespace ks3;

class MyContext
{
public:
    vector<KoiChan> channels;
    vector<shared_ptr<int>> contexts;
    mutex contextMutex;

public:
    static bool AutoAccept(
        KoiChan newChannel,
        void *globalContext,
        weak_ptr<void> &setChannelContext
        ) noexcept
    {
        MyContext &myContext = *(MyContext *)globalContext;
        shared_ptr appContext = make_shared<int>();
        setChannelContext = appContext;

        lock_guard _{myContext.contextMutex};
        myContext.channels.push_back(newChannel);
        myContext.contexts.push_back(appContext);

        return true;
    }

    static void OnReliable0(
        KoiChan channel,
        span<const uint8_t> data,
        [[maybe_unused]] void *globalContext,
        [[maybe_unused]] void *channelContext
        ) noexcept
    {
        string_view sv{(char *)data.data(), data.size()};
        cout << "from " << channel << " reliable channel 0: " << sv << '\n';
        //channel.ReliablePacketSend0(data);
    }

    static void OnReliable1(
        KoiChan channel,
        span<const uint8_t> data,
        [[maybe_unused]] void *globalContext,
        [[maybe_unused]] void *channelContext
        ) noexcept
    {
        string_view sv{(char *)data.data(), data.size()};
        cout << "from " << channel << " reliable channel 1: " << sv << '\n';
        //channel.ReliablePacketSend1(data);
    }

    static void OnReliable2(
        KoiChan channel,
        span<const uint8_t> data,
        [[maybe_unused]] void *globalContext,
        [[maybe_unused]] void *channelContext
        ) noexcept
    {
        string_view sv{(char *)data.data(), data.size()};
        cout << "from " << channel << " reliable channel 2: " << sv << '\n';
        //channel.ReliablePacketSend2(data);
    }

    static void OnReliable3(
        KoiChan channel,
        span<const uint8_t> data,
        [[maybe_unused]] void *globalContext,
        [[maybe_unused]] void *channelContext
        ) noexcept
    {
        string_view sv{(char *)data.data(), data.size()};
        cout << "from " << channel << " reliable channel 3: " << sv << '\n';
        //channel.ReliablePacketSend3(data);
    }

    static void OnUnreliable(
        KoiChan channel,
        span<const uint8_t> data,
        [[maybe_unused]] void *globalContext,
        [[maybe_unused]] void *channelContext
        ) noexcept
    {
        string_view sv{(char *)data.data(), data.size()};
        cout << "from " << channel << " unreliable channel: " << sv << '\n';
        //channel.UnreliablePacketSend(data);
    }

    static void OnDisconnect(
        KoiChan channel,
        void *globalContext,
        [[maybe_unused]] void *channelContext
        ) noexcept
    {
        cout << "disconnected!\n";
        MyContext &myContext = *(MyContext *)globalContext;

        lock_guard _{myContext.contextMutex};
        for (int i = 0; i < myContext.channels.size(); ++i)
        {
            if (myContext.channels[i] == channel)
            {
                myContext.channels.erase(myContext.channels.begin() + i);
                myContext.contexts.erase(myContext.contexts.begin() + i);
                break;
            }
        }
    }
};

int main()
{
    MyContext myContext;
    Kontext ctx;
    KoiSession sess;

    ctx.GlobalContext        = &myContext;
    ctx.OnAccept             = &MyContext::AutoAccept;
    ctx.OnReliableReceive[0] = &MyContext::OnReliable0;
    ctx.OnReliableReceive[1] = &MyContext::OnReliable1;
    ctx.OnReliableReceive[2] = &MyContext::OnReliable2;
    ctx.OnReliableReceive[3] = &MyContext::OnReliable3;
    ctx.OnUnreliableReceive  = &MyContext::OnUnreliable;
    ctx.OnDisconnect         = &MyContext::OnDisconnect;

    uint16_t localSentinel = *sess.Start(ctx);

    cout << "Local Sentinel = " << localSentinel;
    cout << '\n' << R"(Usage:
    quit                   quit this program.
    conn <address+port>    connect to another endpoint.)";
    cout << "\ninput \"quit\" to quit.\n";

    string command;
    thread thr1;
    bool running = true;

    thr1 = thread{[&]()
    {
        auto start_time = steady_clock::now();
        int i = 0;

        //// sleep between 2000ms and 6000ms
        mt19937 randEngine{random_device{}()};
        uniform_int_distribution dist{2000, 6000};
        uniform_int_distribution nchn{0, 3};
        uniform_int_distribution digit{'0' + 0, '0' + 9};

        while (running)
        {
            //this_thread::sleep_for(dist(randEngine) * 1ms);
            this_thread::sleep_until(start_time + i * 16'666'666ns);
            //this_thread::sleep_until(start_time + i * 250ms);
            //this_thread::sleep_until(start_time + i * 2s);
            //this_thread::sleep_until(start_time + i * 999999s);

            lock_guard _{myContext.contextMutex};
            char texts[] {"hello, world! 00"};
            for (auto chan : myContext.channels)
            {
                span<uint8_t> data{(uint8_t *)texts, sizeof(texts) - 1};
                data[14] = (uint8_t)digit(randEngine);
                data[15] = (uint8_t)digit(randEngine);
                int ch = nchn(randEngine);
                chan.ReliablePacketSend(ch, data);
                cout << "sent to " << chan << ' ' << ch << ' ' << texts << '\n';
            }
            ++i;
        }

        for (auto chan : myContext.channels)
        {
            chan.Disconnect();
        }
    }};

    while (getline(cin, command), command != "quit")
    {
        if (command.starts_with("conn") and command.length() > 5)
        {
            string endpoint = command.substr(5);
            cout << "endpoint: " << endpoint << '\n';
            sess.ConnectTo(endpoint, 0);
            continue;
        }
        if (command == "0" || command == "1" ||
            command == "2" || command == "3")
        {
            int index = command[0] - '0';
            for (auto chan : myContext.channels)
            {
                span<uint8_t> data{(uint8_t *)"hello, world!", sizeof("hello, world!") - 1};
                chan.ReliablePacketSend(index, data);
            }
        }
        if (command == "u")
        {
            for (auto chan : myContext.channels)
            {
                span<uint8_t> data{(uint8_t *)"hello, world!", sizeof("hello, world!") - 1};
                chan.UnreliablePacketSend(data);
            }
        }
    }

    running = false;
    thr1.join();
}
