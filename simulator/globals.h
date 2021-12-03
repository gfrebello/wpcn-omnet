#include <stdio.h>
#include <omnetpp.h>
#include <fstream>
#include <string>
#include <map>

using namespace omnetpp;

//setting some macros
#define PREIMAGE_SIZE 32
#define COMMITMENT_BATCH_SIZE 10

extern cTopology *globalTopology;
extern std::map<std::string, std::vector<std::tuple<std::string, double, simtime_t> > > pendingPayments;
extern std::map<std::string, std::map<std::string, std::tuple <double, double, double, int, double, double, cGate*, cGate*> > > nameToPCs;
extern std::map<std::string, std::vector<std::pair<std::string, double> > > adjMatrix;
