#include <cstdio>
#include <ctime>
#include <cstdarg>
#include <cinttypes>
#include <cstdlib>
#include <string>
#include <iostream>
#include <algorithm>
#include <sys/time.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <map>
#include <boost/program_options.hpp>
#include <boost/typeof/typeof.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/exceptions.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/uuid_generators.hpp>

namespace po = boost::program_options;

struct ProbeConf
{
    std::string logdir;
    std::string logname;

    struct ProbeSvr 
    {
        std::string name;
        std::string host;
        unsigned short port;
        unsigned int freq;
        unsigned int pkgsizemin;
        unsigned int pkgsizemax;
    };

    std::vector<ProbeSvr> svrs;
};

struct ProbeCtx
{
    FILE* logfile;
    std::string logname;

    int epollfd;
    std::vector<int> sockets;

    std::vector<boost::shared_ptr<boost::asio::steady_timer>> timers;
    boost::shared_ptr<boost::asio::steady_timer> tickTimer;

    struct ProbePktInfo
    {
        uint64_t sendTimeInMilli;
        std::string name;
        std::string pktUUID;
    };

    std::string dummyUUID;
    std::map<uint64_t, ProbePktInfo> pkts;
};


static ProbeConf gProbeConf;
static ProbeCtx gProbeCtx;
static char gBuffer[32* 1024 * 1024];
static uint64_t gPktID = 0;

// 日志
void logv(const char* fmt, ...)
{
    time_t rawtime = time(NULL);
    tm* ltm = localtime(&rawtime);

    char buf[128] = { 0, };
    snprintf(buf, sizeof(buf), "%s/%s_%04d_%02d_%02d.log", 
        gProbeConf.logdir.c_str(), gProbeConf.logname.c_str(), 
        ltm->tm_year + 1900, ltm->tm_mon + 1, ltm->tm_mday);

    if (gProbeCtx.logname.empty() || (0 != gProbeCtx.logname.compare(buf)))
    {
        if (NULL != gProbeCtx.logfile)
        {
            fclose(gProbeCtx.logfile);
            gProbeCtx.logfile = NULL;
        }

        gProbeCtx.logfile = fopen(buf, "a+");
        if (gProbeCtx.logfile != NULL)
        {
            gProbeCtx.logname = buf;
        }
    }

    if (gProbeCtx.logfile != NULL)
    {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        tm* ltm = localtime(&tv.tv_sec);

        snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%03d", 
            ltm->tm_year + 1900, ltm->tm_mon + 1, ltm->tm_mday,
            ltm->tm_hour, ltm->tm_min, ltm->tm_sec, (int) tv.tv_usec / 1000);

        va_list args;
        va_start(args, fmt);
        fprintf(gProbeCtx.logfile, "%s ", buf);
        vfprintf(gProbeCtx.logfile, fmt, args);
        fprintf(gProbeCtx.logfile, "\n");
        va_end(args);

        va_start(args, fmt);
        printf("%s ", buf);
        vprintf(fmt, args);
        printf("\n");
        va_end(args);
    }
}

uint64_t get_millisecond_time()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint64_t now = ((uint64_t) tv.tv_sec) * 1000 + tv.tv_usec / 1000;
    return now;
}

// 解析参数获取配置文件路径
std::string parse_program_options(int argc, char** argv)
{    
    std::string conf;

    try {
        po::options_description desc("allowed options");
        desc.add_options()
            ("help", "show help message")
            ("conf-file", po::value<std::string>(), "xml configure file");
        po::variables_map vm;
        po::store(po::parse_command_line(argc, argv, desc), vm);
        po::notify(vm);

        if (vm.count("help") || (! vm.count("conf-file")))
        {
            std::cout << desc << std::endl;
            return conf;
        }

        conf = vm["conf-file"].as<std::string>();
    }
    catch (std::exception e)
    {
        std::cerr << "Usage:./udpprobecli --conf-file=path-of-conf" << std::endl;
    }

    return conf;
}

// 解析配置文件
int parse_conf_file(const std::string& conf)
{
    try {
        boost::property_tree::ptree pt;
        read_xml(conf, pt);

        std::string logdir = pt.get<std::string>("udpprobeconf.logdir");
        std::string logname = pt.get<std::string>("udpprobeconf.logname");

        gProbeConf.logdir = logdir;
        gProbeConf.logname = logname;

        logv("logdir %s,logname %s", logdir.c_str(), logname.c_str());

        boost::property_tree::ptree node = pt.get_child("udpprobeconf.probesvrs");
        for (BOOST_AUTO(pos, node.begin()); pos != node.end(); ++pos)
        {
            const boost::property_tree::ptree& sub = pos->second;
            const std::string& name = sub.get<std::string>("<xmlattr>.name");
            const std::string& host = sub.get<std::string>("<xmlattr>.host");
            unsigned short port = sub.get<unsigned short>("<xmlattr>.port");
            unsigned int freq = sub.get<unsigned int>("<xmlattr>.freq");
            unsigned int pkgsizemin = sub.get<unsigned int>("<xmlattr>.pkgsizemin");
            unsigned int pkgsizemax = sub.get<unsigned int>("<xmlattr>.pkgsizemax");
            
            logv("name %s,host %s,port %u,freq %d,pkgsizemin %d,pkgsizemax %d",
                name.c_str(), host.c_str(), port, freq, pkgsizemin, pkgsizemax);

            ProbeConf::ProbeSvr psvr;
            psvr.name = name;
            psvr.host = host;
            psvr.port = port;
            psvr.freq = freq;
            psvr.pkgsizemin = pkgsizemin;
            psvr.pkgsizemax = pkgsizemax;

            gProbeConf.svrs.push_back(psvr);
        }
    }
    catch (const boost::property_tree::ptree_error& e)
    {
        std::cerr << e.what() << std::endl; 
        return -1;
    }

    return 0;
}

void probe_svr(const boost::system::error_code& ec,
               boost::asio::steady_timer* t,
               unsigned int idx)
{
    const ProbeConf::ProbeSvr& psconf = gProbeConf.svrs[idx];
    int sockfd = gProbeCtx.sockets[idx];

    uint32_t nbytes = psconf.pkgsizemin + rand() % (psconf.pkgsizemax - psconf.pkgsizemin);
    nbytes = std::max(32u, nbytes);

    boost::uuids::uuid aUUID = boost::uuids::random_generator()();
    const std::string sUUID = boost::uuids::to_string(aUUID);

    std::string payload = sUUID;

    uint64_t nowInMilli = get_millisecond_time();
    payload.append((char*) &nowInMilli, sizeof(nowInMilli));

    ++gPktID;
    payload.append((char*) &gPktID, sizeof(gPktID));

    for (size_t i = payload.size(); i < nbytes; ++i)
    {
        payload.append(1, rand() % 255);
    }

    struct sockaddr_in svraddr;
    bzero(&svraddr, sizeof(svraddr));
    svraddr.sin_family = AF_INET;
    svraddr.sin_port = htons(psconf.port);
    inet_pton(AF_INET, psconf.host.c_str(), &svraddr.sin_addr);

    sendto(sockfd, payload.c_str(), payload.size(), 0,
           (struct sockaddr*) &svraddr, sizeof(svraddr));

    ProbeCtx::ProbePktInfo pktInfo;
    pktInfo.sendTimeInMilli = nowInMilli;
    pktInfo.name = psconf.name;
    pktInfo.pktUUID = sUUID;
    gProbeCtx.pkts[gPktID] = pktInfo;

    logv("[SEND] name %s,uuid %s,pkt id %" PRIu64",len %" PRIu64,
         psconf.name.c_str(), sUUID.c_str(), gPktID, payload.size());

    unsigned int interval = 60 * 1000 / psconf.freq;
    t->expires_at(t->expiry() + boost::asio::chrono::milliseconds(interval));
    t->async_wait(boost::bind(probe_svr, boost::asio::placeholders::error, t, idx));
}

void handle_recv_pkt(const std::string& pktData)
{
    const std::string& dummyUUID = gProbeCtx.dummyUUID;
    uint64_t sendTimeInMilli = 0;
    uint64_t pktID = 0;

    if (pktData.size() < (dummyUUID.size() + sizeof(sendTimeInMilli) + sizeof(pktID)))
    {
        logv("recv pkt illegal");
        return;
    }

    std::string pktUUID(pktData, 0, dummyUUID.size());
    sendTimeInMilli = *((uint64_t*) (pktData.c_str() + dummyUUID.size()));
    pktID = *((uint64_t*) (pktData.c_str() + dummyUUID.size() + sizeof(sendTimeInMilli)));

    auto got = gProbeCtx.pkts.find(pktID);
    if (got == gProbeCtx.pkts.end())
    {
        // maybe timeout
        return;
    }

    const std::string& name = got->second.name;

    uint64_t nowInMilli = get_millisecond_time();
    uint64_t elapsed = nowInMilli - sendTimeInMilli;

    logv("[RECV] name %s,uuid %s,pkt id %" PRIu64",len %" PRIu64 ",time %" PRIu64,
         name.c_str(), pktUUID.c_str(), pktID, pktData.size(), elapsed);

    gProbeCtx.pkts.erase(pktID);
}

void handle_probe_timeout()
{
    uint64_t period = 60 * 1000;
    uint64_t nowInMilli = get_millisecond_time();

    auto it = gProbeCtx.pkts.begin();
    while (it != gProbeCtx.pkts.end())
    {
        ProbeCtx::ProbePktInfo& pktInfo = it->second;
        uint64_t elapse = nowInMilli - pktInfo.sendTimeInMilli;
        if (elapse > period)
        {
            logv("[TIMEOUT] name %s,uuid %s",
                 pktInfo.name.c_str(), pktInfo.pktUUID.c_str());
            it = gProbeCtx.pkts.erase(it);
        }
        else
        {
            break;
        }
    }
}

void probe_recv()
{
    struct epoll_event events[1024];
    int nfds = epoll_wait(gProbeCtx.epollfd, events, sizeof(events) / sizeof(events[0]), 0);
    if (nfds == -1)
    {
        logv("epoll_wait failed");
        exit(-1);
    }

    for (int i = 0; i < nfds; ++i)
    {
        int sockfd = events[i].data.fd;

        struct sockaddr_in svraddr;
        socklen_t addrlen = sizeof(svraddr);
        int nbytes = recvfrom(sockfd, gBuffer, sizeof(gBuffer), 0,
                (struct sockaddr*) &svraddr, &addrlen);
        if (nbytes < 0)
        {
            logv("recvfrom failed");
            continue;
        }

        std::string pktData(gBuffer, (size_t) nbytes);
        handle_recv_pkt(pktData);
    }
}

void probe_tick(const boost::system::error_code& ec)
{
    probe_recv();

    handle_probe_timeout();

    auto& t = gProbeCtx.tickTimer;
    t->expires_at(t->expiry() + boost::asio::chrono::milliseconds(1));
    t->async_wait(boost::bind(probe_tick, boost::asio::placeholders::error));
}

int main(int argc, char** argv)
{
    srand(time(NULL));

    boost::uuids::uuid aUUID = boost::uuids::random_generator()();
    gProbeCtx.dummyUUID = boost::uuids::to_string(aUUID);

    gProbeCtx.logfile = NULL; 

    std::string conf = parse_program_options(argc, argv); 
    if (conf.empty())
    {
        return -1;
    }

    int ret = parse_conf_file(conf);
    if (0 != ret)
    {
        std::cerr << "parse conf file failed" << std::endl;
        return -1;
    }

    // epoll
    int epollfd = epoll_create(1);
    if (epollfd == -1)
    {
        logv("epoll_create failed,errno %d,%s", errno, strerror(errno));
        return -1;
    }

    gProbeCtx.epollfd = epollfd;

    boost::asio::io_context ioContext;

    for (size_t i = 0; i < gProbeConf.svrs.size(); ++i)
    {
        const ProbeConf::ProbeSvr& psconf = gProbeConf.svrs[i];
        logv("start %s", psconf.name.c_str());

        int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
        if (sockfd < 0)
        {
            logv("socket failed,ret %d", sockfd);
            return -1;
        }

        unsigned int interval = 60 * 1000 / psconf.freq;
        boost::shared_ptr<boost::asio::steady_timer> t(new boost::asio::steady_timer(ioContext, boost::asio::chrono::milliseconds(interval)));
        t->async_wait(boost::bind(probe_svr, boost::asio::placeholders::error, t.get(), i));

        gProbeCtx.sockets.push_back(sockfd);
        gProbeCtx.timers.push_back(t);

        struct epoll_event ev;
        ev.events = EPOLLIN;
        ev.data.fd = sockfd;
        int rc = epoll_ctl(epollfd, EPOLL_CTL_ADD, sockfd, &ev);
        if (rc == -1)
        {
            logv("epoll_ctl failed");
            return -1;
        }
    }

    boost::shared_ptr<boost::asio::steady_timer> tickTimer(new boost::asio::steady_timer(ioContext, boost::asio::chrono::milliseconds(1)));
    tickTimer->async_wait(boost::bind(probe_tick, boost::asio::placeholders::error));
    gProbeCtx.tickTimer = tickTimer;

    ioContext.run();

    logv("exit...");

    return 0;
}
