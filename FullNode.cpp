#include "globals.h"
#include "crypto.h"
#include "updateAddHTLC_m.h"
#include "updateFulfillHTLC_m.h"
#include "failHTLC_m.h"
#include "baseMessage_m.h"
#include "payment_m.h"
#include "PaymentChannel.h"
#include "invoice_m.h"

class FullNode : public cSimpleModule {

    protected:
        virtual void initialize() override;
        virtual void handleMessage(cMessage *msg) override;
        virtual void forwardMessage(BaseMessage *baseMsg);
        virtual void refreshDisplay() const;
//        virtual int getDestinationGate (int destination);

        // Routing functions
        virtual std::vector<std::string> getPath(std::map<std::string, std::string> parents, std::string target);
        virtual std::string minDistanceNode (std::map<std::string, double> distances, std::map<std::string, bool> visited);
        virtual std::vector<std::string> dijkstraWeightedShortestPath (std::string src, std::string target, std::map<std::string, std::vector<std::pair<std::string, double> > > graph);


        std::map<std::string, std::string> _myPreImages; // paymentHash to preImage
        std::map<std::string, std::string> _myInFlights; // paymentHash to nodeName (who owes me)
        //std::map<std::string, int*> _inFlightsPath;
        std::map<std::string, cModule*> _senderModules; // PaymentHash to Module
        //virtual void setPreImage (std::string paymentHash, std::string preImage) { this->_myPreImages[paymentHash] = preImage; };
        //virtual std::string getPreImageByHash (std::string paymentHash) { return this->_myPreImages[paymentHash]; };
        //virtual void setMyInFlight (std::string paymentHash, int destination) { this->_myInFlights[paymentHash] = destination; };
        //virtual int getInFlightChannel (std::string paymentHash) { return this->_myInFlights[paymentHash]; };

//        virtual BaseMessage* generateHTLC (BaseMessage *ttmsg, cModule *sender);
//        virtual BaseMessage* handleInvoice(BaseMessage *ttmsg, cModule *sender);
//        virtual BaseMessage* fulfillHTLC (BaseMessage *ttmsg, cModule *sender);
        virtual Invoice* generateInvoice(std::string srcName, double value);

    public:
        bool _isFirstSelfMessage;
        typedef std::map<std::string, int> RoutingTable;  // nodeName to gateIndex
        RoutingTable rtable;
        std::map<std::string, PaymentChannel> _paymentChannels;
        std::map<std::string, int> _signals;
        cTopology *_localTopology;

        Json::Value paymentChannelstoJson();
        void printPaymentChannels();
        bool updatePaymentChannel(std::string nodeName, double value, bool increase);
};

// Define module and initialize random number generator
Define_Module(FullNode);

std::string myName;

// Util functions
void FullNode::printPaymentChannels() {

    EV << "Printing payment channels of node " << getIndex() << ":\n { ";
    for (auto& it : _paymentChannels) {
        EV << "{ '" << it.first << "': " << it.second.toJson().toStyledString();
    }
    EV << " }\n";
}


Json::Value FullNode::paymentChannelstoJson() {

    Json::Value json;
    std::map<std::string, PaymentChannel>::const_iterator it = _paymentChannels.begin(), end = _paymentChannels.end();
    for ( ; it != end; it++) {
        json[it->first] = it->second.toJson();
    }
    return json;
}


//Omnet++ functions

void FullNode::initialize() {

    // Get name (id) and initialize local topology based on the global topology created by netBuilder
    _localTopology = globalTopology;
    myName = getName();
    std::map<std::string, std::vector<std::tuple<std::string, double, simtime_t>>> localPendingPayments = pendingPayments;

    // Initialize payment channels
    for (auto& neighborToPCs : nameToPCs[myName]) {
        std::string neighborName = neighborToPCs.first;
        std::tuple<double, double, double, int, double, double, cGate*> pcTuple = neighborToPCs.second;
        double capacity = std::get<0>(pcTuple);
        double fee = std::get<1>(pcTuple);
        double quality = std::get<2>(pcTuple);
        int maxAcceptedHTLCs = std::get<3>(pcTuple);
        int numHTLCs = 0;
        double HTLCMinimumMsat = std::get<4>(pcTuple);
        double channelReserveSatoshis = std::get<5>(pcTuple);
        cGate* neighborGate = std::get<6>(pcTuple);


        PaymentChannel pc = PaymentChannel(capacity, fee, quality, maxAcceptedHTLCs, numHTLCs, HTLCMinimumMsat, channelReserveSatoshis, neighborGate);
        _paymentChannels[neighborName] = pc;

        // Register signals and statistics
        std::string signalName = myName +"-to-" + neighborName + ":capacity";
        simsignal_t signal = registerSignal(signalName.c_str());
        _signals[signalName] = signal;
        emit(_signals[signalName], _paymentChannels[neighborName]._capacity);

        std::string statisticName = myName +"-to-" + neighborName + ":capacity";
        cProperty *statisticTemplate = getProperties()->get("statisticTemplate", "pcCapacities");
        getEnvir()->addResultRecorders(this, signal, statisticName.c_str(), statisticTemplate);
    }


    // Build routing table
    cTopology::Node *thisNode = _localTopology->getNodeFor(this);
    for (int i = 0; i < _localTopology->getNumNodes(); i++) {
        if (_localTopology->getNode(i) == thisNode)
            continue;  // skip ourselves

        _localTopology->calculateWeightedSingleShortestPathsTo(_localTopology->getNode(i));

        if (thisNode->getNumPaths() == 0)
            continue;  // not connected

        cGate *parentModuleGate = thisNode->getPath(0)->getLocalGate();
        int gateIndex = parentModuleGate->getIndex();
        std::string nodeName = _localTopology->getNode(i)->getModule()->getName();
        rtable[nodeName] = gateIndex;
        EV << "  towards " << nodeName << " gateIndex is " << gateIndex << endl;
    }

    // Schedule payments according to workload
    std::map<std::string, std::vector<std::tuple<std::string, double, simtime_t>>>::iterator it = pendingPayments.find(myName);
    if (it != pendingPayments.end()) {
        std::vector<std::tuple<std::string, double, simtime_t>> myWorkload = it->second;

        for (const auto& paymentTuple: myWorkload) {

             std::string srcName = std::get<0>(paymentTuple);
             double value = std::get<1>(paymentTuple);
             simtime_t time = std::get<2>(paymentTuple);
             char msgname[32];
             sprintf(msgname, "%s-to-%s;value:%0.1f", srcName.c_str(), myName.c_str(), value);

             // Create payment message
             Payment *trMsg = new Payment(msgname);
             trMsg->setSource(srcName.c_str());
             trMsg->setDestination(myName.c_str());
             trMsg->setValue(value);
             trMsg->setHopCount(0);

             // Create base message
             BaseMessage *baseMsg = new BaseMessage();
             baseMsg->setMessageType(TRANSACTION_INIT);
             baseMsg->setHopCount(0);

             // Encapsulate and schedule
             baseMsg->encapsulate(trMsg);
             scheduleAt(simTime()+time, baseMsg);
             _isFirstSelfMessage = true;
        }
    } else {
        EV << "No workload found for " << myName.c_str() << ".\n";
    }
}

void FullNode::handleMessage(cMessage *msg) {

    BaseMessage *baseMsg = check_and_cast<BaseMessage *>(msg);
//    Payment *ttmsg = check_and_cast<Payment *>(msg);

    // Treat messages according to their message types
    switch(baseMsg->getMessageType()) {

        // Initialize payment
        case TRANSACTION_INIT: {
            Payment *trMsg = check_and_cast<Payment *> (baseMsg->decapsulate());
            EV << "TRANSACTION_INIT received. Starting payment "<< trMsg->getName() << "\n";

            // Create ephemeral communication channel with the payment source
            std::string srcName = trMsg->getSource();
            std::string srcPath = "PCN." + srcName;
            double value = trMsg->getValue();
            cModule* srcMod = getModuleByPath(srcPath.c_str());
            cGate* myGate = this->getOrCreateFirstUnconnectedGate("out", 0, false, true);
            cGate* srcGate = srcMod->getOrCreateFirstUnconnectedGate("in", 0, false, true);
            cDelayChannel *tmpChannel = cDelayChannel::create("tmpChannel");
            tmpChannel->setDelay(100);
            myGate->connectTo(srcGate, tmpChannel);

            // Create invoice and send it to the payment source
            Invoice *invMsg = generateInvoice(srcName, value);
            baseMsg->setMessageType(INVOICE);
            baseMsg->encapsulate(invMsg);
            send(baseMsg, myGate);

            // Close ephemeral connection
            myGate->disconnect();
            break;
        }

        case INVOICE: {
            Invoice *invMsg = check_and_cast<Invoice *> (baseMsg->decapsulate());
            EV << "INVOICE received. Payment hash: " << invMsg->getPaymentHash() << "\n";

            //std::string dstName = invMsg->getSenderModule()->getName();
            std::string dstName = invMsg->getDestination();
            std::string srcName = getName();

            // Find route to destination
            std::vector<std::string> path = this->dijkstraWeightedShortestPath(srcName, dstName, adjMatrix);

            // Print route
            std::string printPath = "Full route to destination: ";
            for (auto hop: path)
                printPath = printPath + hop + ", ";
            printPath += "\n";
            EV << printPath;

            break;
        }
        case UPDATE_ADD_HTLC:
            EV << "UPDATE_ADD_HTLC received.\n";
            break;
        case UPDATE_FAIL_HTLC:
            EV << "UPDATE_FAIL_HTLC received.\n";
            break;
        case UPDATE_FULFILL_HTLC:
            EV << "UPDATE_FULFILL_HTLC received.\n";
            break;

            break;
        case REVOKE_AND_ACK:
            EV << "REVOKE_AND_ACK received.\n";
            break;
        case COMMITMENT_SIGNED:
            EV << "COMMITMENT_SIGNED received.\n";
            break;
    }

}


// Helper function to update payment channels. If increase = true, the function attemps to increase
// the capacity of the channel. Else, check whether the channel has enough capacity to process the payment.
bool FullNode::updatePaymentChannel (std::string nodeName, double value, bool increase) {
    if(increase) {
        _paymentChannels[nodeName].increaseCapacity(value);
        return true;
    } else {
        double capacity = _paymentChannels[nodeName]._capacity;
        if(capacity - value < 0)
            return false;
        else {
            _paymentChannels[nodeName].decreaseCapacity(value);
            return true;
        }
    }
}

void FullNode::forwardMessage(BaseMessage *baseMsg) {

//    std::string dstName = msg->getDestination();
//    std::string prevName = msg->getSenderModule()->getName();
//    myName = getName();
//
//    // Check if this node is the destination
//    if (dstName == myName){
//        EV << "Message reached destinaton at " << myName.c_str() << " after "
//                << msg->getHopCount() << " hops. Finishing...\n";
//        updatePaymentChannel(prevName, msg->getValue(), true);
//        std::string signalName = myName + "-to-" + prevName + ":capacity";
//        emit(_signals[signalName], _paymentChannels[prevName]._capacity);
//        if (hasGUI()) {
//            char text[64];
//            sprintf(text, "Payment reached destination!");
//            bubble(text);
//        }
//        delete msg;
//        return;
//    }
//
//    // Check if there's a route to the destination
//    RoutingTable::iterator it = rtable.find(dstName);
//    if (it == rtable.end()) {
//        EV << dstName << " unreachable, discarding packet " << msg->getName() << endl;
//        delete msg;
//        //return;
//    }
//    int outGateIndex = (*it).second;
//    std::string nextName = gate("out",outGateIndex)->getPathEndGate()->getOwnerModule()->getName();
//
//    // Update channel capacities and emit signals
//    if (prevName != myName) { // Prevent self messages from interfering in channel capacities
//        EV << "forwarding packet " << msg->getName() << " on gate index " << outGateIndex << endl;
//        updatePaymentChannel(prevName, msg->getValue(), true);
//        std::string signalName = myName + "-to-" + prevName + ":capacity";
//        emit(_signals[signalName], _paymentChannels[prevName]._capacity);
//
//        if(updatePaymentChannel(nextName, msg->getValue(), false)) {
//            signalName = myName + "-to-" + nextName + ":capacity";
//            emit(_signals[signalName],_paymentChannels[nextName]._capacity);
//            msg->setHopCount(msg->getHopCount()+1);
//            send(msg, "out", outGateIndex);
//        } else { // Not enough capacity to forward. Fail the payment.
//            EV << "Not enough capacity to forward payment. Failing the payment...\n";
//            delete msg;
//        }
//
//    } else if(_isFirstSelfMessage == true) {
//
//        if(updatePaymentChannel(nextName, msg->getValue(), false)) {
//             std::string signalName = myName + "-to-" + nextName + ":capacity";
//             emit(_signals[signalName],_paymentChannels[nextName]._capacity);
//             _isFirstSelfMessage = false;
//             msg->setHopCount(msg->getHopCount()+1);
//             send(msg, "out", outGateIndex);
//        } else {
//            EV << "Not enough capacity to initialize payment. Failing the payment...\n";
//            delete msg;
//        }
//    } else {
//
//    }
}

void FullNode::refreshDisplay() const {

    char pcText[100];
    sprintf(pcText, " ");
    for(auto& it : _paymentChannels) {
        char buf[20];
        std::string neighborName = it.first;
        float capacity = it.second.getCapacity();
        sprintf(buf, "%s: %0.1f,\n ", neighborName.c_str(), capacity);
        strcat(pcText,buf);
    }
    getDisplayString().setTagArg("t", 0, pcText);
}


/***********************************************************************************************************************/
/* generateHTLC creates a hashed time lock contract between two nodes that share a payment channel. The function       */
/* receives as inputs the payment hash that will lock the payment, the amount to be paid, and the next hop to forward  */
/* the message. The function saves the new created htlc as in-flight, which is a key-value map that indexes every htlc */
/* by the payment hash.                                                                                                */
/***********************************************************************************************************************/
//BaseMessage* FullNode::generateHTLC(BaseMessage *ttmsg, cModule *sender) { // aka Handle add HTLC
//    updateAddHTLC *message = check_and_cast<updateAddHTLC *> (ttmsg->getEncapsulatedPacket());
//
//    std::string paymentHash;
//    std::string source = getName();
//    paymentHash.assign(message->getPaymentHash());
//    double amount = message->getAmount();
//    //int *hops = ttmsg->getHops();
//    int hopCount = ttmsg->getHopCount();
//    int destination = ttmsg->getHops(hopCount+1);
//
//    double current_balance = _paymentChannels[destination].getCapacity();
//    double htlc_minimum_msat = _paymentChannels[destination].getHTLCMinimumMSAT();
//    int htlc_number = _paymentChannels[destination].getHTLCNumber();
//    int max_acceptd_htlcs = _paymentChannels[destination].getMaxAcceptedHTLCs();
//    double channelReserveSatoshis = _paymentChannels[destination].getChannelReserveSatoshis();
//
//    //verifies if the channel follows all the requirements to support the new htlc
//    if (current_balance == 0 || htlc_number + 1 > max_acceptd_htlcs || amount < htlc_minimum_msat || current_balance - amount < channelReserveSatoshis){
//        return NULL;
//    }
//
//    // Decrease channel capacity and increase number of HTLCs in the channel
//    _paymentChannels[destination].decreaseCapacity(amount); // Substitute for updatePaymentChannel
//    _paymentChannels[destination].increaseHTLCNumber();
//
//    //store the new created htlc
//    _paymentChannels[destination].setInFlight(paymentHash, amount);
//    //_myInFlights[paymentHash] = destination;
//    //_inFlightsPath[paymentHash] = hops;
//    _senderModules[paymentHash] = sender;
//
//    updateAddHTLC *htlc = new updateAddHTLC();
//    htlc->setSource(source);
//    //htlc->setDestination(destination);
//    htlc->setPaymentHash(paymentHash.c_str());
//    htlc->setAmount(amount);
//    //htlc->setHops(&hops);
//    //htlc->setHopCount(hopCount+1);
//
//    BaseMessage *newMessage = new BaseMessage();
//    newMessage->setDestination(destination);
//    newMessage->setMessageType(UPDATE_ADD_HTLC);
//    newMessage->setHopCount(hopCount+1);
//
//    newMessage.encapsute(htlc);
//
//    return newMessage;
//
//}
//
//BaseMessage* FullNode::generateFailHTLC(BaseMessage *ttmsg, cModule *sender){
//    std::string reason;
//
//    reason.assign("Could not create HTLC");
//    FailHTLC *failHTLC = new FailHTLC();
//    failHTLC->setErrorReason(reason.c_str());
//
//    BaseMessage *newMessage = new BaseMessage();
//    newMessage->setDestination(sender->getIndex());
//    newMessage->setMessageType(UPDATE_FAIL_HTLC);
//    newMessage->setHopCount(0);
//
//    newMessage.encapsulate(failHTLC);
//
//    return newMessage;
//
//}
//
////modify fulfillHTLC to analyse the preImage, update own channel, and forward message
//BaseMessage* FullNode::fulfillHTLC(BaseMessage *ttmsg, cModule *sender){
//
//    update_fulfill_htlc *message = check_and_cast<update_fulfill_htlc *> (ttmsg->getEncapsulatedPacket());
//    int hopCount = ttmsg->getHopCount();
//
//    std::string paymentHash;
//    std::string preImage;
//
//    paymentHash.assign(message->getPaymentHash());
//    preImage.assign(message->getPreImage());
//    std::string preImageHash = sha256(preImage);
//
//    if (preImageHash != paymentHash){
//        return NULL;
//    }
//
//    std::string source = getName();
//    std::string senderName = sender->getName();
//
////    if (_myInFlights[paymentHash] != senderName){
////        return NULL;
////    }
//
//    double amount = _paymentChannels[senderName].getInFlight(paymentHash);
//    cModule *prevNodeHTLC = _senderModules[paymentHash];
//    std::string prevNodeName = prevNodeHTLC->getName();
//    _paymentChannels[prevNodeName].increaseCapacity(amount);
//    //sender->_paymentChannels[source].increaseCapacity(amount);
//
//    _paymentChannels[senderName].removeInFlight(paymentHash);
//    //_myInFlights.erase(paymentHash);
//
//    update_fulfill_htlc *htlcPreImage = new update_fulfill_htlc();
//    htlcPreImage->setPaymentHash(paymentHash.c_str());
//    htlcPreImage->setPreImage(preImage.c_str());
//    //htlcPreImage->setHopCount(hopCount+1);
//
//    BaseMessage *fulfillMessage = new BaseMessage();
//    fulfillMessage->setHopCount(hopCount+1);
//
//    BaseMessage.encapsulate(htlcPreImage);
//
//    return fulfillMessage;
//
//}
//
//BaseMessage* FullNode::handleInvoice (BaseMessage *ttmsg, cModule *sender){
//    Invoice *invoice = check_and_cast<Invoice *> (ttmsg->getEncapsulatedPacket());
//
//    double amount = invoice->getAmount();
//    std::string paymentHash;
//    paymentHash.assign(invoice->getPaymentHash());
//
//    //add path finding function here
//
//    updateAddHTLC *first_htlc = new updateAddHTLC();
//    first_htlc->setSource(getName());
//    first_htlc->setPaymentHash(paymentHash.c_str());
//    first_htlc->setAmount(amount);
//
//    BaseMessage *newMessage = new BaseMessage();
//    newMessage->setDestination(ttmsg->getSource());
//    newMessage->setHopCount(0);
//    newMessage->setMessageType(UPDATE_ADD_HTLC);
//
//    newMessage.encapsulate(first_htlc);
//    return newMessage;
//
//}
//


Invoice* FullNode::generateInvoice(std::string srcName, double value) {

    //std::string srcName = getName();
    std::string preImage;
    std::string preImageHash;

    //preImage = genRandom();
    preImage = generatePreImage();
    preImageHash = sha256(preImage);

    _myPreImages[preImageHash] = preImage;

    Invoice *invoice = new Invoice();
    invoice->setSource(srcName.c_str());
    invoice->setDestination(getName());
    invoice->setValue(value);
    invoice->setPaymentHash(preImageHash.c_str());

    return invoice;
}

// Selects the node with minimum distance out of a distances list while disregarding visited nodes
std::string FullNode::minDistanceNode (std::map<std::string, double> distances, std::map<std::string, bool> visited) {

    // Initialize min value
    double minDist = INT_MAX;
    std::string minName = "";

    for (auto& node : distances) {
        std::string nodeName = node.first;
        double dist = node.second;
        bool isNodeVisited = visited.find(nodeName)->second;
        if (!isNodeVisited && dist <= minDist) {
            minDist = dist;
            minName = nodeName;
        }
    }
    if (minDist == INT_MAX){
        return NULL;
    }
    else{
        return minName;
    }

}

// Return the path to source given a target and its parent nodes
std::vector<std::string> FullNode::getPath (std::map<std::string, std::string> parents, std::string target) {

    std::vector<std::string> path;
    std::string node = target;

    // Recursively traverse the parents path
    while (parents[node] != node) {
        path.insert(path.begin(), node);
        node = parents[node];
    }

    // Set ourselves as the source node
    path.insert(path.begin(), getName());

    return path;
}

// This function returns the Dijkstra's shortest path from a source to some target given an adjacency matrix
std::vector<std::string> FullNode::dijkstraWeightedShortestPath (std::string src, std::string target, std::map<std::string, std::vector<std::pair<std::string, double> > > graph) {

    int numNodes = graph.size();
    std::map<std::string, double> distances;
    std::map<std::string, bool> visited;
    std::map<std::string, std::string> parents; // a.k.a nodeToParent

    // Initialize distances as infinite and visited array as false
    for (auto & node : graph) {
        distances[node.first] = INT_MAX;
        visited[node.first] = false;
        parents[node.first] = "";
    }

    // Set source node distance to zero and parent to itself
    distances[src] = 0;
    parents[src] = src;

    for (int i = 0; i < numNodes-1; i++) {
        std::string node = minDistanceNode(distances, visited);
        visited[node] = true;

        std::vector<std::pair<std::string,double>>::iterator it;

        // Update distance value of neighbor nodes of the current node
        for (it = graph[node].begin(); it != graph[node].end(); it++){
            std::string neighbor = it->first;
            double linkWeight = it->second;
            if (!visited[neighbor]) {
                if(distances[node] + linkWeight < distances[neighbor]) {
                    parents[neighbor] = node;
                    distances[neighbor] = distances[node] + linkWeight;
                }
            }
        }
    }
    return getPath(parents, target);
}


//
//
//// getDestinationGate returns the gate associated with the destination received as argument
//int FullNode::getDestinationGate (int destination){
//    for (cModule::GateIterator i(this); !i.end(); i++) {
//        cGate *gate = *i;
//        owner = gate("out")->getOwnerModule()->getIndex();
//        if (owner == destination){
//            break;
//        }
//    }
//    return owner;
//}
//
