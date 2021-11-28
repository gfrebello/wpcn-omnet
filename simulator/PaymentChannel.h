#include <stdio.h>
#include <omnetpp.h>
#include <vector>
#include <queue>

#include "globals.h"
#include "updateAddHTLC_m.h"
#include "commitmentSigned_m.h"

using namespace omnetpp;

class PaymentChannel {

    public:
        double _capacity;
        double _fee;
        double _quality;

        //some channel parameters defined in BOLT#2
        int _maxAcceptedHTLCs;
        double _HTLCMinimumMsat;
        int _numHTLCs;
        double _channelReserveSatoshis;
        std::map<std::string, double> _inFlights;
        std::map<std::string, UpdateAddHTLC *> _pendingHTLCs; //paymentHash to HTLC
        std::queue<std::string> _pendingHTLCsFIFO; //determines the order that the HTLCs were added
        std::map<int, std::vector<UpdateAddHTLC *>> _HTLCsWaitingForAck; //ackId to HTLCs waiting for ack to arrive
        std::map<std::string, std::string> _previousHop; //paymentHash to previous Hop

        cGate *_localGate;
        cGate *_neighborGate;

        // Constructors for polymorphism
        PaymentChannel() {};
        PaymentChannel(cGate *gate);
        //PaymentChannel(double capacity, double fee, double quality, cGate *gate);
        PaymentChannel(double capacity, double balance, double quality, int maxAcceptedHTLCs, int numHTLCs, double HTLCMinimumMsat, double channelReserveSatoshis, cGate* localGate, cGate *neighborGate);

        // Getters and setters
         virtual double getCapacity() const { return this->_capacity; };
         virtual void setCapacity(double capacity) { this->_capacity = capacity; };
         virtual void increaseCapacity(double value) { this->_capacity = this->_capacity + value; };
         virtual void decreaseCapacity(double value) { this->_capacity = this->_capacity - value; };
         virtual double getFee() const { return this->_fee; };
         virtual void setFee(double fee) { this->_fee = fee; };
         virtual double getQuality() const { return this->_quality; };
         virtual void setQuality(double quality) { this->_quality = quality; };
         virtual int getMaxAcceptedHTLCs() const { return this->_maxAcceptedHTLCs; };
         virtual void setMaxAcceptedHTLCs(int maxAcceptedHTLCs) { this->_maxAcceptedHTLCs = maxAcceptedHTLCs; };
         virtual double getHTLCMinimumMSAT() const { return this->_HTLCMinimumMsat; };
         virtual void setHTLCMinimumMSAT(double HTLCMinimumMsat) { this->_HTLCMinimumMsat = HTLCMinimumMsat; };
         virtual int getnumHTLCs() const { return this->_numHTLCs; };
         virtual void setnumHTLCs (int numHTLCs) { this->_numHTLCs = numHTLCs; };
         virtual void increasenumHTLCs () { this->_numHTLCs = _numHTLCs + 1; };
         virtual void decreasenumHTLCs () { this->_numHTLCs = _numHTLCs - 1; };
         virtual double getChannelReserveSatoshis () const { return this->_channelReserveSatoshis; };
         virtual void setChannelReserveSatothis (double channelReserveSatoshis) { this->_channelReserveSatoshis = channelReserveSatoshis; };
         virtual void setInFlight (std::string paymentHash, double amount) { this->_inFlights[paymentHash] = amount; };
         virtual double getInFlight (std::string paymentHash) { return this->_inFlights[paymentHash]; };
         virtual void removeInFlight (std::string paymentHash) { this->_inFlights.erase(paymentHash); };
         virtual void setPendingHTLC (std::string paymentHash, UpdateAddHTLC *htlc) { this->_pendingHTLCs[paymentHash] = htlc; };
         virtual UpdateAddHTLC* getPendingHTLC (std::string paymentHash) { return this->_pendingHTLCs[paymentHash]; };
         virtual void removePendingHTLC (std::string paymentHash) { this->_pendingHTLCs.erase(paymentHash); };
         virtual void setPendingHTLCFIFO (std::string paymentHash) { this->_pendingHTLCsFIFO.push(paymentHash); };
         virtual std::string getPendingHTLCFIFO () { return _pendingHTLCsFIFO.front(); };
         virtual void removePendingHTLCFIFO () { this->_pendingHTLCsFIFO.pop(); };
         virtual size_t getPendingBatchSize () { return this->_pendingHTLCsFIFO.size(); };
         virtual std::vector<UpdateAddHTLC *> getHTLCsWaitingForAck (int id) { return this->_HTLCsWaitingForAck[id]; };
         virtual void setHTLCsWaitingForAck (int id, std::vector<UpdateAddHTLC *> vector) { this->_HTLCsWaitingForAck[id] = vector;};
         virtual void removeHTLCsWaitingForAck (int id) { this->_HTLCsWaitingForAck.erase(id); };
         virtual void setPreviousHop (std::string paymentHash, std::string previousHop) { this->_previousHop[paymentHash] = previousHop; };
         virtual std::string getPreviousHop (std::string paymentHash) { return this->_previousHop[paymentHash]; };
         virtual void removePreviousHop (std::string paymentHash) { this->_previousHop.erase(paymentHash); };
         virtual cGate* getLocalGate() const { return this->_localGate; };
         virtual void setLocalGate(cGate* gate) { this->_localGate = gate; };
         virtual cGate* getNeighborGate() const { return this->_neighborGate; };
         virtual void setNeighborGate(cGate* gate) { this->_neighborGate = gate; };

        // Auxiliary functions
        Json::Value toJson() const;

    private:
        void copy(const PaymentChannel& other);

};




