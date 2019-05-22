#include <cstdio>
#include <ctime>
#include <cstdarg>
#include <string>
#include <iostream>
#include <sys/time.h>
#include <boost/program_options.hpp>
#include <boost/typeof/typeof.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/exceptions.hpp>

namespace po = boost::program_options;

struct ProbeConf
{
    std::string logdir;
    std::string logname;

    struct ProbeSvr 
    {
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
};

ProbeConf gProbeConf;
ProbeCtx gProbeCtx;

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
    }
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
            const std::string& host = sub.get<std::string>("<xmlattr>.host");
            unsigned short port = sub.get<unsigned short>("<xmlattr>.port");
            unsigned int freq = sub.get<unsigned int>("<xmlattr>.freq");
            unsigned int pkgsizemin = sub.get<unsigned int>("<xmlattr>.pkgsizemin");
            unsigned int pkgsizemax = sub.get<unsigned int>("<xmlattr>.pkgsizemax");
            
            logv("host %s,port %u,freq %d,pkgsizemin %d,pkgsizemax %d", 
                host.c_str(), port, freq, pkgsizemin, pkgsizemax);

            ProbeConf::ProbeSvr psvr;
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

int main(int argc, char** argv)
{
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

    logv("1111");

    return 0;
}
