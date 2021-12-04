#include "globals.h"
#include "crypto.h"
#include "baseMessage_m.h"
#include "payment_m.h"
#include "PaymentChannel.h"
#include "invoice_m.h"
#include "commitmentSigned_m.h"
#include "revokeAndAck_m.h"
#include "HTLCRefused_m.h"
#include "HTLC.h"

class FullNode : public cSimpleModule {

    protected:
        // Protected data structures
        std::map<std::string, std::string> _myPreImages; // paymentHash to preImage
        std::map<std::string, std::string> _myInFlights; // paymentHash to nodeName (who owes me)
        std::map<std::string, std::string> _myPayments; //paymentHash to status (PENDING, COMPLETED or FAILED)
        std::map<std::string, BaseMessage *> _myStoredMessages; // paymentHash to baseMsg (for finding reverse path)
        std::map<std::string, cModule*> _senderModules; // paymentHash to Module

        // Omnetpp functions
        virtual void initialize() override;
        virtual void handleMessage(cMessage *msg) override;
        virtual void refreshDisplay() const;

        // Routing functions
        virtual std::vector<std::string> getPath(std::map<std::string, std::string> parents, std::string target);
        virtual std::string minDistanceNode (std::map<std::string, double> distances, std::map<std::string, bool> visited);
        virtual std::vector<std::string> dijkstraWeightedShortestPath (std::string src, std::string target, std::map<std::string, std::vector<std::pair<std::string, double> > > graph);

        // Message handlers
        virtual void initHandler (BaseMessage *baseMsg);
        virtual void invoiceHandler (BaseMessage *baseMsg);
        virtual void updateAddHTLCHandler (BaseMessage *baseMsg);
        virtual void updateFulfillHTLCHandler (BaseMessage *baseMsg);
        virtual void updateFailHTLCHandler (BaseMessage *baseMsg);
        virtual void HTLCRefusedHandler (BaseMessage *baseMsg);
        virtual void commitSignedHandler (BaseMessage *baseMsg);
        virtual void revokeAndAckHandler (BaseMessage *baseMsg);

        // HTLC senders
        virtual void sendFirstFulfillHTLC (HTLC *htlc, std::string firstHop);
        virtual void sendFirstFailHTLC (HTLC *htlc, std::string firstHop);

        // HTLC committers
        virtual void commitUpdateAddHTLC (HTLC *htlc, std::string neighbor);
        virtual void commitUpdateFulfillHTLC (HTLC *htlc, std::string neighbor);
        virtual void commitUpdateFailHTLC (HTLC *htlc, std::string neighbor);

        // Util functions
        virtual bool tryUpdatePaymentChannel(std::string nodeName, double value, bool increase);
        virtual double getCapacityAfterPendingHTLCs (std::string nodeName);
        virtual bool tryCommitTxOrFail(std::string, bool);
        virtual Invoice* generateInvoice(std::string srcName, double value);
        virtual void setInFlight(std::string paymentHash, std::string nextHop);

        // Old code pending removal
        //std::map<std::string, int*> _inFlightsPath;
        //virtual void setPreImage (std::string paymentHash, std::string preImage) { this->_myPreImages[paymentHash] = preImage; };
        //virtual std::string getPreImageByHash (std::string paymentHash) { return this->_myPreImages[paymentHash]; };
        //virtual void setMyInFlight (std::string paymentHash, int destination) { this->_myInFlights[paymentHash] = destination; };
        //virtual int getInFlightChannel (std::string paymentHash) { return this->_myInFlights[paymentHash]; };
        //virtual void forwardFailtHTLC(UpdateFailHTLC *htlc, std::string nextHop);

    public:
        // Public data structures
        bool _isFirstSelfMessage;
        cTopology *_localTopology;
        int localCommitCounter;
        typedef std::map<std::string, int> RoutingTable;  // neighborName to gateIndex
        RoutingTable rtable;
        std::map<std::string, PaymentChannel> _paymentChannels; // neighborName to PaymentChannel
        std::map<std::string, int> _signals; // myName to signal
};

// Define module and initialize random number generator
Define_Module(FullNode);

/***********************************************************************************************************************/
/* OMNETPP FUNCTIONS                                                                                                   */
/***********************************************************************************************************************/

void FullNode::initialize() {

    // Get name (id) and initialize local topology based on the global topology created by netBuilder
    _localTopology = globalTopology;
    this->localCommitCounter = 0;
    std::string myName = getName();
    std::map<std::string, std::vector<std::tuple<std::string, double, simtime_t>>> localPendingPayments = pendingPayments;

    // Initialize payment channels
    for (auto& neighborToPCs : nameToPCs[myName]) {
        std::string neighborName = neighborToPCs.first;
        std::tuple<double, double, double, int, double, double, cGate*, cGate*> pcTuple = neighborToPCs.second;
        double capacity = std::get<0>(pcTuple);
        double fee = std::get<1>(pcTuple);
        double quality = std::get<2>(pcTuple);
        int maxAcceptedHTLCs = std::get<3>(pcTuple);
        int numHTLCs = 0;
        double HTLCMinimumMsat = std::get<4>(pcTuple);
        double channelReserveSatoshis = std::get<5>(pcTuple);
        cGate* localGate = std::get<6>(pcTuple);
        cGate* neighborGate = std::get<7>(pcTuple);

        PaymentChannel pc = PaymentChannel(capacity, fee, quality, maxAcceptedHTLCs, numHTLCs, HTLCMinimumMsat, channelReserveSatoshis, localGate, neighborGate);
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
             char msgname[100];
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

    // Treat messages according to their message types
    switch(baseMsg->getMessageType()) {

        case TRANSACTION_INIT: {
            initHandler(baseMsg);
            break;
        }
        case INVOICE: {
            invoiceHandler(baseMsg);
            break;
        }
        case UPDATE_ADD_HTLC: {
            updateAddHTLCHandler(baseMsg);
            break;
        }
        case UPDATE_FULFILL_HTLC: {
            updateFulfillHTLCHandler(baseMsg);
            break;
        }
        case UPDATE_FAIL_HTLC: {
            updateFailHTLCHandler(baseMsg);
            break;
        }
        case HTLC_REFUSED: {
            HTLCRefusedHandler(baseMsg);
            break;
        }
        case COMMITMENT_SIGNED: {
            commitSignedHandler(baseMsg);
            break;
        }
        case REVOKE_AND_ACK: {
            revokeAndAckHandler(baseMsg);
            break;
        }
    }
}

void FullNode::refreshDisplay() const {

    for(auto& it : _paymentChannels) {
        char buf[30];
        std::string neighborName = it.first;
        std::string neighborPath = "PCN." + neighborName;
        float capacity = it.second.getCapacity();
        cGate *gate = it.second.getLocalGate();
        cChannel *channel = gate->getChannel();
        sprintf(buf, "%0.1f\n", capacity);
        channel->getDisplayString().setTagArg("t", 0, buf);
        channel->getDisplayString().setTagArg("t", 1, "l");
    }
}


/***********************************************************************************************************************/
/* ROUTING FUNCTIONS                                                                                                   */
/***********************************************************************************************************************/

std::string FullNode::minDistanceNode (std::map<std::string, double> distances, std::map<std::string, bool> visited) {
    // Selects the node with minimum distance out of a distances list while disregarding visited nodes

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

std::vector<std::string> FullNode::getPath (std::map<std::string, std::string> parents, std::string target) {
    // Return the path to source given a target and its parent nodes

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

std::vector<std::string> FullNode::dijkstraWeightedShortestPath (std::string src, std::string target, std::map<std::string, std::vector<std::pair<std::string, double> > > graph) {
    // This function returns the Dijkstra's shortest path from a source to some target given an adjacency matrix

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


/***********************************************************************************************************************/
/* MESSAGE HANDLERS                                                                                                    */
/***********************************************************************************************************************/

void FullNode::initHandler (BaseMessage *baseMsg) {

    Payment *initMsg = check_and_cast<Payment *> (baseMsg->decapsulate());
    EV << "TRANSACTION_INIT received. Starting payment "<< initMsg->getName() << "\n";

    // Create ephemeral communication channel with the payment source
    std::string srcName = initMsg->getSource();
    std::string srcPath = "PCN." + srcName;
    double value = initMsg->getValue();
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
    baseMsg->setName("INVOICE");
    send(baseMsg, myGate);

    // Close ephemeral connection
    myGate->disconnect();
}

void FullNode::invoiceHandler (BaseMessage *baseMsg) {

    Invoice *invMsg = check_and_cast<Invoice *> (baseMsg->decapsulate());
    EV << "INVOICE received. Payment hash: " << invMsg->getPaymentHash() << "\n";

    std::string dstName = invMsg->getDestination();
    std::string myName = getName();
    std::string paymentHash = invMsg->getPaymentHash();

    // Add payment into payment list and set status = pending
    _myPayments[paymentHash] = "PENDING";

    // Find route to destination
    std::vector<std::string> path = this->dijkstraWeightedShortestPath(myName, dstName, adjMatrix);
    std::string firstHop = path[1];

    // Print route
    std::string printPath = "Full route to destination: ";
    for (auto hop: path)
        printPath = printPath + hop + ", ";
    printPath += "\n";
    EV << printPath;

    //Create HTLC
    EV << "Creating HTLC to kick off the payment process \n";
    BaseMessage *newMessage = new BaseMessage();
    newMessage->setDestination(dstName.c_str());
    newMessage->setMessageType(UPDATE_ADD_HTLC);
    newMessage->setHopCount(1);
    newMessage->setHops(path);
    newMessage->setName("UPDATE_ADD_HTLC");
    newMessage->setDisplayString("i=block/encrypt;is=s");

    UpdateAddHTLC *firstUpdateAddHTLC = new UpdateAddHTLC();
    firstUpdateAddHTLC->setSource(myName.c_str());
    firstUpdateAddHTLC->setPaymentHash(invMsg->getPaymentHash());
    firstUpdateAddHTLC->setValue(invMsg->getValue());

    HTLC *firstHTLC = new HTLC(firstUpdateAddHTLC);
    _paymentChannels[firstHop].setPendingHTLC(invMsg->getPaymentHash(), firstHTLC);
    _paymentChannels[firstHop].setLastPendingHTLCFIFO(invMsg->getPaymentHash());
    _paymentChannels[firstHop].setPreviousHop(invMsg->getPaymentHash(), myName);

    newMessage->encapsulate(firstUpdateAddHTLC);
    cGate *gate = _paymentChannels[firstHop].getLocalGate();

    //Sending HTLC out
    EV << "Sending HTLC to " + firstHop + " with payment hash " + firstUpdateAddHTLC->getPaymentHash() + "\n";
    send(newMessage, gate);

}

void FullNode::updateAddHTLCHandler (BaseMessage *baseMsg) {

    EV << "UPDATE_ADD_HTLC received at " + std::string(getName()) + " from " + std::string(baseMsg->getSenderModule()->getName()) + ".\n";
    std::string myName = getName();

    // If the message is a self message, it means we already attempted to commit changes but failed because the batch size was insufficient. So we wait for the timeout.
    // Otherwise, we attempt to commit normally.
    if (!baseMsg->isSelfMessage()){

        // Decapsulate message and get path
        UpdateAddHTLC *updateAddHTLCMsg = check_and_cast<UpdateAddHTLC *> (baseMsg->decapsulate());
        std::string dstName = baseMsg->getDestination();
        std::vector<std::string> path = baseMsg->getHops();
        std::string sender = baseMsg->getSenderModule()->getName();

        // Create new HTLC in the backward direction and set it as pending
        HTLC *htlcBackward = new HTLC(updateAddHTLCMsg);
        EV << "Storing UPDATE_ADD_HTLC from node " + sender + " as pending.\n";
        _paymentChannels[sender].setPendingHTLC(updateAddHTLCMsg->getPaymentHash(), htlcBackward);
        _paymentChannels[sender].setLastPendingHTLCFIFO(updateAddHTLCMsg->getPaymentHash());
        _paymentChannels[sender].setPreviousHop(updateAddHTLCMsg->getPaymentHash(), sender);

        // If I'm the destination, trigger commit immediately and return
        if (dstName == this->getName()){
            EV << "Payment reached its destination. Not forwarding.\n";

            // Store base message for retrieving path on the first fulfill message later
            _myStoredMessages[updateAddHTLCMsg->getPaymentHash()] = baseMsg;

            if (!tryCommitTxOrFail(sender, false)){
                EV << "Setting timeout for node " + myName + "\n";
                scheduleAt((simTime() + SimTime(500,SIMTIME_MS)),baseMsg);
            }
            return;
        }

        // If I'm not the destination, forward the message to the next hop in the UPSTREAM path
        std::string nextHop = path[baseMsg->getHopCount() + 1];
        std::string previousHop = path[baseMsg->getHopCount()-1];
        std::string paymentHash = updateAddHTLCMsg->getPaymentHash();
        double value = updateAddHTLCMsg->getValue();

        // Check if we have sufficient funds before forwarding
        if ((getCapacityAfterPendingHTLCs(nextHop) - updateAddHTLCMsg->getValue()) < 0) {
            // Not enough capacity to forward payment. Remove pending HTLCs and send a HTLC_REFUSED message to the previous hop.
            _paymentChannels[sender].removePendingHTLC(paymentHash);
            _paymentChannels[sender].removeLastPendingHTLCFIFO();
            _paymentChannels[sender].removePreviousHop(paymentHash);

            BaseMessage *newMessage = new BaseMessage();
            newMessage->setDestination(previousHop.c_str());
            newMessage->setMessageType(HTLC_REFUSED);
            newMessage->setHopCount(baseMsg->getHopCount() - 1);
            newMessage->setHops(path);
            newMessage->setName("HTLC_REFUSED");
            newMessage->setDisplayString("i=status/stop");

            HTLCRefused *newHTLCRefusedMsg = new HTLCRefused();
            newHTLCRefusedMsg->setPaymentHash(updateAddHTLCMsg->getPaymentHash());
            newHTLCRefusedMsg->setErrorReason("INSUFFICIENT CAPACITY.");
            newHTLCRefusedMsg->setValue(value);

            newMessage->encapsulate(newHTLCRefusedMsg);

            cGate *gate = _paymentChannels[previousHop].getLocalGate();
            EV << "Sending HTLC_REFUSED to " + path[(newMessage->getHopCount())] + " with payment hash " + newHTLCRefusedMsg->getPaymentHash() + "\n";
            send(newMessage, gate);

        } else {
            // Enough funds. Forward HTLC.
            EV << "Creating HTLC to kick off the payment process \n";
            BaseMessage *newMessage = new BaseMessage();
            newMessage->setDestination(dstName.c_str());
            newMessage->setMessageType(UPDATE_ADD_HTLC);
            newMessage->setHopCount(baseMsg->getHopCount() + 1);
            newMessage->setHops(path);
            newMessage->setDisplayString("i=block/encrypt;is=s");
            newMessage->setName("UPDATE_ADD_HTLC");

            UpdateAddHTLC *newUpdateAddHTLC = new UpdateAddHTLC();
            newUpdateAddHTLC->setSource(myName.c_str());
            newUpdateAddHTLC->setPaymentHash(updateAddHTLCMsg->getPaymentHash());
            newUpdateAddHTLC->setValue(value);

            HTLC *htlcForward = new HTLC(newUpdateAddHTLC);

            // Add HTLC as pending in the forward direction and set previous hop as ourselves
            _paymentChannels[nextHop].setPendingHTLC(newUpdateAddHTLC->getPaymentHash(), htlcForward);
            _paymentChannels[nextHop].setLastPendingHTLCFIFO(newUpdateAddHTLC->getPaymentHash());
            _paymentChannels[nextHop].setPreviousHop(updateAddHTLCMsg->getPaymentHash(), getName());

            newMessage->encapsulate(newUpdateAddHTLC);

            cGate *gate = _paymentChannels[nextHop].getLocalGate();

            //Sending HTLC out
            EV << "Sending HTLC to " + path[(newMessage->getHopCount())] + " with payment hash " + newUpdateAddHTLC->getPaymentHash() + "\n";
            send(newMessage, gate);

            if (!tryCommitTxOrFail(sender, false)){
                EV << "Setting timeout for node " + myName + ".\n";
                scheduleAt((simTime() + SimTime(500,SIMTIME_MS)),baseMsg);
            }
        }

    } else {
        // The message is the result of a timeout.
        EV << myName + " timeout expired. Creating commit.\n";
        std::vector<std::string> path = baseMsg->getHops();
        std::string previousHop = path[baseMsg->getHopCount()-1];
        std::string dstName = baseMsg->getDestination();
        tryCommitTxOrFail(previousHop, true);
    }
}

void FullNode::updateFulfillHTLCHandler (BaseMessage *baseMsg) {

    EV << "UPDATE_FULFILL_HTLC received at " + std::string(getName()) + " from " + std::string(baseMsg->getSenderModule()->getName()) + ".\n";
    std::string myName = getName();

    // If the message is a self message, it means we already attempted to commit changes but failed because the batch size was insufficient. So we wait for the timeout.
    // Otherwise, we attempt to commit normally.
    if (!baseMsg->isSelfMessage()){
        // Decapsulate message, get path, and preimage
        UpdateFulfillHTLC *fulfillHTLC = check_and_cast<UpdateFulfillHTLC *> (baseMsg->decapsulate());
        std::string dstName = baseMsg->getDestination();
        std::vector<std::string> path = baseMsg->getHops();
        std::string sender = baseMsg->getSenderModule()->getName();
        std::string paymentHash = fulfillHTLC->getPaymentHash();
        std::string preImage = fulfillHTLC->getPreImage();
        double value = fulfillHTLC->getValue();

         // Verify preimage
         if (sha256(preImage) != paymentHash){
             throw std::invalid_argument("ERROR: Failed to fulfill HTLC. Different hash value.");
         }

         // Create new HTLC in the backward direction and set it as pending
         HTLC *htlcBackward = new HTLC(fulfillHTLC);
         EV << "Storing UPDATE_FULFILL_HTLC from node " + sender + " as pending.\n";
         _paymentChannels[sender].setPendingHTLC(paymentHash, htlcBackward);
         _paymentChannels[sender].setLastPendingHTLCFIFO(paymentHash);
         _paymentChannels[sender].setPreviousHop(paymentHash, sender);

         // If we are the destination, just try to commit the payment and return
         if (getName() == dstName) {
             EV << "Payment fulfillment has reached the payment's origin. Trying to commit...\n";
             if (!tryCommitTxOrFail(sender, false)) {
                 EV << "Setting timeout for node " + myName + "\n";
                 scheduleAt((simTime() + SimTime(500,SIMTIME_MS)),baseMsg);
             }
             return;
         }

        // If we're not the destination, forward the message to the next hop in the DOWNSTREAM path
        std::string nextHop = path[baseMsg->getHopCount()-1];
        std::string previousHop = path[baseMsg->getHopCount()+1];

        EV << "Forwarding UPDATE_FULFILL_HTLC in the downstream direction...\n";
        BaseMessage *newMessage = new BaseMessage();
        newMessage->setDestination(dstName.c_str());
        newMessage->setMessageType(UPDATE_FULFILL_HTLC);
        newMessage->setHopCount(baseMsg->getHopCount()-1);
        newMessage->setHops(path);
        newMessage->setDisplayString("i=block/decrypt;is=s");
        newMessage->setName("UPDATE_FULFILL_HTLC");

        UpdateFulfillHTLC *forwardFulfillHTLC = new UpdateFulfillHTLC();
        forwardFulfillHTLC->setPaymentHash(paymentHash.c_str());
        forwardFulfillHTLC->setPreImage(preImage.c_str());
        forwardFulfillHTLC->setValue(value);

        // Set UPDATE_FULFILL_HTLC as pending and invert the previous hop (now we're going downstream)
        HTLC *forwardBaseHTLC  = new HTLC(forwardFulfillHTLC);
        _paymentChannels[nextHop].setPendingHTLC(paymentHash, forwardBaseHTLC);
        _paymentChannels[nextHop].setLastPendingHTLCFIFO(paymentHash);
        _paymentChannels[nextHop].removePreviousHop(paymentHash);
        _paymentChannels[nextHop].setPreviousHop(paymentHash, myName);

        newMessage->encapsulate(forwardFulfillHTLC);

        cGate *gate = _paymentChannels[nextHop].getLocalGate();

        //Sending HTLC out
        EV << "Sending preimage " + preImage + " to " + path[(newMessage->getHopCount())] + "for payment hash " + paymentHash + "\n";
        send(newMessage, gate);

        // Try to commit
        if (!tryCommitTxOrFail(sender, false)){
            EV << "Setting timeout for node " + myName + ".\n";
            scheduleAt((simTime() + SimTime(500,SIMTIME_MS)),baseMsg);
        }

    } else {
        // The message is the result of a timeout.
        EV << myName + " timeout expired. Creating commit.\n";
        std::vector<std::string> path = baseMsg->getHops();
        std::string previousHop = path[baseMsg->getHopCount()+1];
        tryCommitTxOrFail(previousHop, true);
    }
}

void FullNode::updateFailHTLCHandler (BaseMessage *baseMsg) {

    EV << "UPDATE_FAIL_HTLC received at " + std::string(getName()) + " from " + std::string(baseMsg->getSenderModule()->getName()) + ".\n";
    std::string myName = getName();
    std::vector<std::string> path = baseMsg->getHops();
    std::string dstName = baseMsg->getDestination();

    // If the message is a self message, it means we already attempted to commit changes but failed because the batch size was insufficient. So we wait for the timeout.
    // Otherwise, we attempt to commit normally.
    if (!baseMsg->isSelfMessage()) {

        // Decapsulate message, get path, and preimage
        UpdateFailHTLC *failHTLC = check_and_cast<UpdateFailHTLC *> (baseMsg->decapsulate());
        std::string sender = baseMsg->getSenderModule()->getName();
        std::string paymentHash = failHTLC->getPaymentHash();
        std::string errorReason = failHTLC->getErrorReason();
        double value = failHTLC->getValue();

        // Create new HTLC in the backward direction and set it as pending
        HTLC *htlcBackward = new HTLC(failHTLC);
        EV << "Storing UPDATE_FAIL_HTLC from node " + sender + " as pending.\n";
        _paymentChannels[sender].setPendingHTLC(paymentHash, htlcBackward);
        _paymentChannels[sender].setLastPendingHTLCFIFO(paymentHash);
        _paymentChannels[sender].setPreviousHop(paymentHash, sender);

        // If we are the destination, just try to commit and return
        if (getName() == dstName) {
            EV << "Payment fail has reached the payment's origin. Trying to commit...\n";
            if (!tryCommitTxOrFail(sender, false)) {
                EV << "Setting timeout for node " + myName + "\n";
                scheduleAt((simTime() + SimTime(500,SIMTIME_MS)),baseMsg);
            }
            return;
        }

        // If we're not the destination, forward the message to the next hop in the DOWNSTREAM path
        std::string nextHop = path[baseMsg->getHopCount()-1];
        std::string previousHop = path[baseMsg->getHopCount()+1];

        EV << "Forwarding UPDATE_FAIL_HTLC in the downstream direction...\n";
        BaseMessage *newMessage = new BaseMessage();
        newMessage->setDestination(dstName.c_str());
        newMessage->setMessageType(UPDATE_FAIL_HTLC);
        newMessage->setHopCount(baseMsg->getHopCount()-1);
        newMessage->setHops(path);
        newMessage->setDisplayString("i=status/stop");
        newMessage->setName("UPDATE_FAIL_HTLC");

        UpdateFailHTLC *forwardFailHTLC = new UpdateFailHTLC();
        forwardFailHTLC->setPaymentHash(paymentHash.c_str());
        forwardFailHTLC->setErrorReason(errorReason.c_str());
        forwardFailHTLC->setValue(value);

        // Set UPDATE_FAIL_HTLC as pending and invert the previous hop (now we're going downstream)
        HTLC *forwardBaseHTLC  = new HTLC(forwardFailHTLC);
        _paymentChannels[nextHop].setPendingHTLC(paymentHash, forwardBaseHTLC);
        _paymentChannels[nextHop].setLastPendingHTLCFIFO(paymentHash);
        _paymentChannels[nextHop].removePreviousHop(paymentHash);
        _paymentChannels[nextHop].setPreviousHop(paymentHash, myName);

        newMessage->encapsulate(forwardFailHTLC);

        cGate *gate = _paymentChannels[nextHop].getLocalGate();

        //Sending HTLC out
        EV << "Sending UPDATE_FAIL_HTLC to " + path[(newMessage->getHopCount())] + "for payment hash " + paymentHash + "\n";
        send(newMessage, gate);

        // Try to commit
        if (!tryCommitTxOrFail(sender, false)){
            EV << "Setting timeout for node " + myName + ".\n";
            scheduleAt((simTime() + SimTime(500,SIMTIME_MS)),baseMsg);
        }

    } else {
        // The message is the result of a timeout.
        EV << myName + " timeout expired. Creating commit.\n";
        int hopCount = baseMsg->getHopCount();
        std::string previousHop = path[baseMsg->getHopCount()+1];
        tryCommitTxOrFail(previousHop, true);
    }
}

void FullNode::HTLCRefusedHandler (BaseMessage *baseMsg) {

    EV << "HTLC_REFUSED received at " + std::string(getName()) + " from " + std::string(baseMsg->getSenderModule()->getName()) + ".\n";

    HTLCRefused *HTLCRefusedMsg = check_and_cast<HTLCRefused *> (baseMsg->decapsulate());
    std::string sender = baseMsg->getSenderModule()->getName();
    std::string paymentHash = HTLCRefusedMsg->getPaymentHash();
    std::string errorReason = HTLCRefusedMsg->getErrorReason();
    double value = HTLCRefusedMsg->getValue();

    EV << "Error reason: " + errorReason + ". Undoing updates...\n";

    if(!_paymentChannels[sender].isPendingHTLC(paymentHash)) {
        // If we don't find the HTLC in our pending list, we look into our committed HTLCs list.
        if (!_paymentChannels[sender].isInFlight(paymentHash)) {
            // The received HTLC is neither pending nor in flight. Something unexpected happened...
            throw std::invalid_argument( "ERROR: Unknown HTLC_REFUSED received!" );
        } else {
            // The HTLC is in flight. This cannot happen.
            throw std::invalid_argument( "ERROR: Refused HTLC is already in flight!" );
        }
    } else {
        // The HTLC is still pending so we need to remove it upstream before the next commitment and trigger UPDATE_FAIL_HTLC downstream.
        _paymentChannels[sender].removePendingHTLC(paymentHash);
        _paymentChannels[sender].removePendingHTLCFIFOByValue(paymentHash);
        _paymentChannels[sender].removePreviousHop(paymentHash);

        // Store base message for retrieving path on the first fail message
        _myStoredMessages[paymentHash] = baseMsg;


        std::vector<std::string> path = baseMsg->getHops();
        std::string nextHop = path[baseMsg->getHopCount()-1];

        UpdateFailHTLC *failHTLC = new UpdateFailHTLC();
        failHTLC->setPaymentHash(paymentHash.c_str());
        failHTLC->setValue(value);
        failHTLC->setErrorReason(errorReason.c_str());

        HTLC *baseHTLC = new HTLC(failHTLC);
        sendFirstFailHTLC(baseHTLC, nextHop);
    }
}

void FullNode::commitSignedHandler (BaseMessage *baseMsg) {
    EV << "COMMITMENT_SIGNED received at " + std::string(getName()) + " from " + std::string(baseMsg->getSenderModule()->getName()) + ".\n";
    commitmentSigned *commitMsg = check_and_cast<commitmentSigned *>(baseMsg->decapsulate());

    HTLC *htlc = NULL;
    std::string myName = getName();
    std::string paymentHash;
    unsigned short index = 0;
    std::vector<HTLC *> HTLCs = commitMsg->getHTLCs();
    size_t numberHTLCs = HTLCs.size();
    std::string sender = baseMsg->getSenderModule()->getName();

    // Iterate through the received HTLC and attempt to commit them
    for (index = 0; index < numberHTLCs; index++) {
        htlc = HTLCs[index];
        paymentHash = htlc->getPaymentHash();
        double value = htlc->getValue();
        int htlcType = htlc->getType();

        // If the HTLC is not in the pending HTLC list nor inFlight, skip it (it's meant for the next commitment)
        if (!_paymentChannels[sender].getPendingHTLC(paymentHash) && !_paymentChannels[sender].getInFlight(paymentHash) ){
            throw std::invalid_argument( "ERROR: Unknown HTLC received!" );
            //continue;
        }

        switch(htlc->getType()) {
            case UPDATE_ADD_HTLC: {
                commitUpdateAddHTLC(htlc, sender);
                break;
            }
            case UPDATE_FULFILL_HTLC: {
                commitUpdateFulfillHTLC(htlc, sender);
                break;
            }
            case UPDATE_FAIL_HTLC: {
                commitUpdateFailHTLC(htlc, sender);
                break;
            }
        }
     }

    std::string signalName = myName +"-to-" + sender + ":capacity";
    emit(_signals[signalName], _paymentChannels[sender]._capacity);

    revokeAndAck *ack = new revokeAndAck();
    ack->setAckId(commitMsg->getId());

    BaseMessage *newMessage = new BaseMessage();
    newMessage->setDestination(sender.c_str());
    newMessage->setMessageType(REVOKE_AND_ACK);
    newMessage->setHopCount(0);
    newMessage->setName("REVOKE_AND_ACK");
    //newMessage->setUpstreamDirection(!baseMsg->getUpstreamDirection());

    newMessage->encapsulate(ack);

    cGate *gate = _paymentChannels[sender].getLocalGate();

    //Sending pre image out
    EV << "Sending ack to " + sender + "with id " + std::to_string(commitMsg->getId()) + "\n";
    send(newMessage, gate);
}

void FullNode::revokeAndAckHandler (BaseMessage *baseMsg) {

    EV << "REVOKE_AND_ACK received at " + std::string(getName()) + " from " + std::string(baseMsg->getSenderModule()->getName()) + ".\n";
    revokeAndAck *ackMsg = check_and_cast<revokeAndAck *> (baseMsg->decapsulate());

    std::string sender = baseMsg->getSenderModule()->getName();
    int ackId = ackMsg->getAckId();
    std::vector<HTLC *> HTLCs = _paymentChannels[sender].getHTLCsWaitingForAck(ackId);
    HTLC *htlc;
    std::string paymentHash;
    size_t index = 0;

    for (index = 0; index < HTLCs.size(); index++){
        htlc = HTLCs[index];
        paymentHash = htlc->getPaymentHash();

        switch(htlc->getType()) {

            case UPDATE_ADD_HTLC: {
                commitUpdateAddHTLC(htlc, sender);
                break;
            }
            case UPDATE_FULFILL_HTLC: {
                commitUpdateFulfillHTLC(htlc, sender);
                break;
            }

            case UPDATE_FAIL_HTLC: {
                commitUpdateFailHTLC(htlc, sender);
                break;
            }
        }
    }
    _paymentChannels[sender].removeHTLCsWaitingForAck(ackId);

}


/***********************************************************************************************************************/
/* HTLC SENDERS                                                                                                        */
/***********************************************************************************************************************/

void FullNode::sendFirstFulfillHTLC (HTLC *htlc, std::string firstHop) {
    // This function creates and sends an UPDATE_FULFILL_HTLC to the first hop in the downstream direction, triggering the beginning of payment completion

    EV << "Payment reached its destination. Releasing preimage.. \n";

    //Get the stored pre image
    std::string paymentHash = htlc->getPaymentHash();
    std::string preImage = _myPreImages[paymentHash];
    BaseMessage *storedBaseMsg = _myStoredMessages[paymentHash];
    std::vector<std::string> path = storedBaseMsg->getHops();
    std::string myName = getName();


    //Generate an UPDATE_FULFILL_HTLC message
    BaseMessage *newMessage = new BaseMessage();
    newMessage->setDestination(path[0].c_str());
    newMessage->setMessageType(UPDATE_FULFILL_HTLC);
    newMessage->setHopCount(storedBaseMsg->getHopCount() - 1);
    newMessage->setHops(storedBaseMsg->getHops());
    newMessage->setName("UPDATE_FULFILL_HTLC");
    newMessage->setDisplayString("i=block/decrypt;is=s");

    UpdateFulfillHTLC *firstFulfillHTLC = new UpdateFulfillHTLC();
    firstFulfillHTLC->setPaymentHash(paymentHash.c_str());
    firstFulfillHTLC->setPreImage(preImage.c_str());
    firstFulfillHTLC->setValue(htlc->getValue());

    // Set UPDATE_FULFILL_HTLC as pending and invert the previous hop (now we're going downstream)
    HTLC *baseHTLC  = new HTLC(firstFulfillHTLC);
    _paymentChannels[firstHop].setPendingHTLC(paymentHash, baseHTLC);
    _paymentChannels[firstHop].setLastPendingHTLCFIFO(paymentHash);
    _paymentChannels[firstHop].removePreviousHop(paymentHash);
    _paymentChannels[firstHop].setPreviousHop(paymentHash, myName);

    newMessage->encapsulate(firstFulfillHTLC);

    cGate *gate = _paymentChannels[firstHop].getLocalGate();

    _myPreImages.erase(paymentHash);
    _myStoredMessages.erase(paymentHash);

    //Sending HTLC out
    EV << "Sending pre image " + preImage + " to " + path[(newMessage->getHopCount()-1)] + "for payment hash " + paymentHash + "\n";
    send(newMessage, gate);
}

void FullNode::sendFirstFailHTLC (HTLC *htlc, std::string firstHop) {
    // This function creates and sends an UPDATE_FAIL_HTLC to the first hop in the downstream direction, triggering the beginning of payment failure

    EV << "Payment FAILED. Initializing downstream unlocking of HTLCs... \n";

    //Get the stored base message
    std::string myName = getName();
    std::string paymentHash = htlc->getPaymentHash();
    BaseMessage *storedBaseMsg = _myStoredMessages[paymentHash];
    std::vector<std::string> failPath = storedBaseMsg->getHops();
    double value = htlc->getValue();

    //Generate an UPDATE_FAIL_HTLC message
    BaseMessage *newMessage = new BaseMessage();
    newMessage->setDestination(failPath[0].c_str());
    newMessage->setMessageType(UPDATE_FAIL_HTLC);
    newMessage->setHopCount(storedBaseMsg->getHopCount()-1);
    newMessage->setHops(failPath);
    newMessage->setName("UPDATE_FAIL_HTLC");
    newMessage->setDisplayString("i=status/stop");

    UpdateFailHTLC *firstFailHTLC = new UpdateFailHTLC();
    firstFailHTLC->setPaymentHash(paymentHash.c_str());
    firstFailHTLC->setValue(htlc->getValue());
    firstFailHTLC->setErrorReason(htlc->getErrorReason().c_str());

    // Set UPDATE_FAIL_HTLC as pending and invert the previous hop (now we're going downstream)
    HTLC *baseHTLC  = new HTLC(firstFailHTLC);
    _paymentChannels[firstHop].setPendingHTLC(paymentHash, baseHTLC);
    _paymentChannels[firstHop].setLastPendingHTLCFIFO(paymentHash);
    _paymentChannels[firstHop].removePreviousHop(paymentHash);
    _paymentChannels[firstHop].setPreviousHop(paymentHash, myName);

    newMessage->encapsulate(firstFailHTLC);

    cGate *gate = _paymentChannels[firstHop].getLocalGate();

    _myPreImages.erase(paymentHash);
    _myStoredMessages.erase(paymentHash);

    //Sending HTLC out
    EV << "Sending first UPDATE_FAIL_HTLC to " + failPath[(newMessage->getHopCount())] + "for payment hash " + paymentHash + "\n";
    send(newMessage, gate);
}


/***********************************************************************************************************************/
/* HTLC COMMITTERS                                                                                                     */
/***********************************************************************************************************************/

void FullNode::commitUpdateAddHTLC (HTLC *htlc, std::string neighbor) {

    std::string myName = getName();
    std::string paymentHash = htlc->getPaymentHash();
    EV << "Committing UPDATE_ADD_HTLC at node " + myName + " with payment hash " + paymentHash + "...\n";
    std::string previousHop = _paymentChannels[neighbor].getPreviousHop(paymentHash);

    // If our neighbor is the HTLC's previous hop, we should commit but not set inFlight (that's the neighbors's responsibility)
    if (_paymentChannels[neighbor].getPreviousHop(paymentHash) == neighbor) {

        // If we are the destination, trigger first UPDATE_FULFILL_HTLC function
        if (!_myPreImages[paymentHash].empty()) {
            sendFirstFulfillHTLC(htlc, neighbor);
        // Otherwise just commit
        } else {
            // Remove from pending list and queue
            _paymentChannels[neighbor].removePendingHTLC(paymentHash);
            _paymentChannels[neighbor].removePendingHTLCFIFOByValue(paymentHash);
        }
    // If our neighbor is the HTLC's next hop, we must set it as in flight and decrement the channel balance
    } else if (_paymentChannels[neighbor].getPreviousHop(paymentHash) == getName()) {
        setInFlight(paymentHash, neighbor);

        // Remove from pending list and queue
        _paymentChannels[neighbor].removePendingHTLC(paymentHash);
        _paymentChannels[neighbor].removePendingHTLCFIFOByValue(paymentHash);

    // If either case is satisfied, this is unexpected behavior
    } else {
        throw std::invalid_argument("ERROR: Could not commit UPDATE_ADD_HTLC. Reason: previousHop unknown.");
    }
}

void FullNode::commitUpdateFulfillHTLC (HTLC *htlc, std::string neighbor) {

    std::string myName = getName();
    std::string paymentHash = htlc->getPaymentHash();
    EV << "Committing UPDATE_FULFILL_HTLC at node " + myName + " with payment hash " + paymentHash + "...\n";
    std::string previousHop = _paymentChannels[neighbor].getPreviousHop(paymentHash);
    double value = htlc->getValue();

    // If our neighbor is the Fulfill's previous hop, we should claim our money
    if (_paymentChannels[neighbor].getPreviousHop(paymentHash) == neighbor) {
        _paymentChannels[neighbor].removeInFlight(paymentHash);

        // Remove from pending list and queue
        _paymentChannels[neighbor].removePendingHTLC(paymentHash);
        _paymentChannels[neighbor].removePendingHTLCFIFOByValue(paymentHash);

        // If we are the destination, the payment has completed successfully
        if(_myPayments[paymentHash] == "PENDING") {
            bubble("Payment completed!");
            EV << "Payment " + paymentHash + " completed!\n";
            _myPayments[paymentHash] = "COMPLETED";
        }

    // If our neighbor is the Fulfill's next hop, we must remove the in flight HTLCs
    } else if (_paymentChannels[neighbor].getPreviousHop(paymentHash) == getName()) {
        tryUpdatePaymentChannel(neighbor, value, true);

        // Remove from pending list and queue
        _paymentChannels[neighbor].removePendingHTLC(paymentHash);
        _paymentChannels[neighbor].removePendingHTLCFIFOByValue(paymentHash);

    // If either case is satisfied, this is unexpected behavior
    } else {
        throw std::invalid_argument("ERROR: Could not commit UPDATE_FULFILL_HTLC. Reason: previousHop unknown.");
    }
}

void FullNode::commitUpdateFailHTLC (HTLC *htlc, std::string neighbor) {

    std::string myName = getName();
    std::string paymentHash = htlc->getPaymentHash();
    EV << "Committing UPDATE_FAIL_HTLC at node " + myName + " with payment hash " + paymentHash + "...\n";
    std::string previousHop = _paymentChannels[neighbor].getPreviousHop(paymentHash);
    double value = htlc->getValue();

    // If our neighbor is the fail's previous hop, we should we must remove the in flight HTLCs and claim our money back
    if (_paymentChannels[neighbor].getPreviousHop(paymentHash) == neighbor) {
        _paymentChannels[neighbor].removeInFlight(paymentHash);
        tryUpdatePaymentChannel(neighbor, value, true);

        // Remove from pending list and queue
        _paymentChannels[neighbor].removePendingHTLC(paymentHash);
        _paymentChannels[neighbor].removePendingHTLCFIFOByValue(paymentHash);

        // If we are the destination, the payment has completed successfully
        if(_myPayments[paymentHash] == "PENDING") {
            bubble("Payment failed!");
            EV << "Payment " + paymentHash + " failed!\n";
            _myPayments[paymentHash] = "FAIL";
        }

    // If our neighbor is the fails's next hop, just remove from pending (the updates have been applied in the sender node)
    } else if (_paymentChannels[neighbor].getPreviousHop(paymentHash) == getName()) {

        // Remove from pending list and queue
        _paymentChannels[neighbor].removePendingHTLC(paymentHash);
        _paymentChannels[neighbor].removePendingHTLCFIFOByValue(paymentHash);

    // If either case is satisfied, this is unexpected behavior
    } else {
        throw std::invalid_argument("ERROR: Could not commit UPDATE_FAIL_HTLC. Reason: previousHop unknown.");
    }
}


/***********************************************************************************************************************/
/* UTIL FUNCTIONS                                                                                                      */
/***********************************************************************************************************************/

bool FullNode::tryUpdatePaymentChannel (std::string nodeName, double value, bool increase) {
    // Helper function to update payment channels. If increase = true, the function attemps to increase
    // the capacity of the channel. Else, check whether the channel has enough capacity to process the payment.

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

double FullNode::getCapacityAfterPendingHTLCs (std::string nodeName) {
    // Helper function to calculate the payment channel capacity after applying pending HTLCs. This is mostly used
    // to discover if a node has sufficient funds to forward a payment.

    std::map<std::string, HTLC*> pendingHTLCs = _paymentChannels[nodeName].getPendingHTLCs();
    double capacity = _paymentChannels[nodeName].getCapacity();
    for (const auto & htlc : pendingHTLCs) {
        if (htlc.second->_type == UPDATE_ADD_HTLC)
            capacity -= htlc.second->_value;
        else
            capacity += htlc.second->_value;
    }
    return capacity;
}

bool FullNode::tryCommitTxOrFail(std::string sender, bool timeoutFlag) {
    /***********************************************************************************************************************/
    /* tryCommitOrFail verifies whether the pending transactions queue has reached the defined commitment batch size       */
    /* or not. If the batch is full, tryCommitOrFail creates a commitment_signed message and sends it to the node that     */
    /* shares the payment channel where the batch is full. If the batch has not yet reached its limit, tryCommitOrFail     */
    /* does nothing.                                                                                                       */
    /***********************************************************************************************************************/

    unsigned short int index = 0;
    HTLC *htlc = NULL;
    std::vector<HTLC *> HTLCVector;
    std::string paymentHash;
    bool through = false;

    EV << "Entered tryCommitTxOrFail. Current batch size: " + std::to_string(_paymentChannels[sender].getPendingBatchSize()) + "\n";

    if (_paymentChannels[sender].getPendingBatchSize() >= COMMITMENT_BATCH_SIZE || timeoutFlag == true) {
        for (index = 0; index < (_paymentChannels[sender].getPendingBatchSize()); index++){
            paymentHash.assign(_paymentChannels[sender].getFirstPendingHTLCFIFO());
            htlc = _paymentChannels[sender].getPendingHTLC(paymentHash);
            HTLCVector.push_back(htlc);
        }

        EV << "Setting through to true\n";
        through = true;

        commitmentSigned *commitTx = new commitmentSigned();
        commitTx->setHTLCs(HTLCVector);
        commitTx->setId(localCommitCounter);

        _paymentChannels[sender].setHTLCsWaitingForAck(localCommitCounter, HTLCVector);

        localCommitCounter += 1;
        //int gateIndex = rtable[sender];
        cGate *gate = _paymentChannels[sender].getLocalGate();

        BaseMessage *baseMsg = new BaseMessage();
        baseMsg->setDestination(sender.c_str());
        baseMsg->setMessageType(COMMITMENT_SIGNED);
        baseMsg->setHopCount(0);
        baseMsg->setName("COMMITMENT_SIGNED");

        baseMsg->encapsulate(commitTx);

        EV << "Sending Commitment Signed from node " + std::string(getName()) + "to " + sender + ". localCommitCounter: " + std::to_string(localCommitCounter) + "\n";

        send (baseMsg, gate);
    }
    return through;
}

Invoice* FullNode::generateInvoice(std::string srcName, double value) {

    //std::string srcName = getName();
    std::string preImage;
    std::string preImageHash;

    //preImage = genRandom();
    preImage = generatePreImage();
    preImageHash = sha256(preImage);

    EV<< "Generated pre image " + preImage + " with hash " + preImageHash + "\n";

    _myPreImages[preImageHash] = preImage;

    Invoice *invoice = new Invoice();
    invoice->setSource(srcName.c_str());
    invoice->setDestination(getName());
    invoice->setValue(value);
    invoice->setPaymentHash(preImageHash.c_str());

    return invoice;
}

void FullNode::setInFlight(std::string paymentHash, std::string nextHop) {
    // Sets payment in flight and removes from pending

    HTLC *htlc = _paymentChannels[nextHop].getPendingHTLC(paymentHash);

    // Put HTLC in flight and decrease capacity on the forward direction
    if (!tryUpdatePaymentChannel(nextHop, htlc->getValue(), false)) {
        // Insufficient funds. Trigger UPDATE_FAIL_HTLC
        throw std::invalid_argument("ERROR: Could not commit UPDATE_ADD_HTLC. Reason: Insufficient funds.");
    }
    _paymentChannels[nextHop].setInFlight(paymentHash, htlc);
    //_paymentChannels[nextHop].removePreviousHop(paymentHash);
    EV << "Payment hash " + paymentHash + "set in flight.";
}





// Old code pending removal
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
//    _paymentChannels[destination].decreaseCapacity(amount); // Substitute for tryUpdatePaymentChannel
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
//    double value = invoice->getValue();
//    std::string paymentHash;
//    paymentHash.assign(invoice->getPaymentHash());
//
//
//    updateAddHTLC *first_htlc = new updateAddHTLC();
//    first_htlc->setSource(getName());
//    first_htlc->setPaymentHash(paymentHash.c_str());
//    first_htlc->setValue(value);
//
//    BaseMessage *newMessage = new BaseMessage();
//    newMessage->setDestination(sender->getName());
//    newMessage->setHopCount(0);
//    newMessage->setMessageType(UPDATE_ADD_HTLC);
//    newMessage->setHops();
//
//    newMessage.encapsulate(first_htlc);
//    return newMessage;
//
//}
//


//void FullNode::forwardMessage(BaseMessage *baseMsg) {
//
//    std::string dstName = msg->getDestination();
//    std::string prevName = msg->getSenderModule()->getName();
//    myName = getName();
//
//    // Check if this node is the destination
//    if (dstName == myName){
//        EV << "Message reached destinaton at " << myName.c_str() << " after "
//                << msg->getHopCount() << " hops. Finishing...\n";
//        tryUpdatePaymentChannel(prevName, msg->getValue(), true);
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
//        tryUpdatePaymentChannel(prevName, msg->getValue(), true);
//        std::string signalName = myName + "-to-" + prevName + ":capacity";
//        emit(_signals[signalName], _paymentChannels[prevName]._capacity);
//
//        if(tryUpdatePaymentChannel(nextName, msg->getValue(), false)) {
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
//        if(tryUpdatePaymentChannel(nextName, msg->getValue(), false)) {
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
//}

