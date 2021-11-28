//
// Generated file, do not edit! Created by nedtool 5.7 from routeReply.msg.
//

#ifndef __ROUTEREPLY_M_H
#define __ROUTEREPLY_M_H

#if defined(__clang__)
#  pragma clang diagnostic ignored "-Wreserved-id-macro"
#endif
#include <omnetpp.h>

// nedtool version check
#define MSGC_VERSION 0x0507
#if (MSGC_VERSION!=OMNETPP_VERSION)
#    error Version mismatch! Probably this file was generated by an earlier version of nedtool: 'make clean' should help.
#endif



// cplusplus {{
#include "messages.h"
#include <vector>
// }}

/**
 * Class generated from <tt>routeReply.msg:6</tt> by nedtool.
 * <pre>
 * packet RouteReply
 * {
 *     int origin;
 *     double routingFee;
 * 	//std::vector<int> path;    
 *     int messageType;
 *     // TODO: maybe add some other infos
 * }
 * </pre>
 */
class RouteReply : public ::omnetpp::cPacket
{
  protected:
    int origin;
    double routingFee;
    int messageType;

  private:
    void copy(const RouteReply& other);

  protected:
    // protected and unimplemented operator==(), to prevent accidental usage
    bool operator==(const RouteReply&);

  public:
    RouteReply(const char *name=nullptr, short kind=0);
    RouteReply(const RouteReply& other);
    virtual ~RouteReply();
    RouteReply& operator=(const RouteReply& other);
    virtual RouteReply *dup() const override {return new RouteReply(*this);}
    virtual void parsimPack(omnetpp::cCommBuffer *b) const override;
    virtual void parsimUnpack(omnetpp::cCommBuffer *b) override;

    // field getter/setter methods
    virtual int getOrigin() const;
    virtual void setOrigin(int origin);
    virtual double getRoutingFee() const;
    virtual void setRoutingFee(double routingFee);
    virtual int getMessageType() const;
    virtual void setMessageType(int messageType);
};

inline void doParsimPacking(omnetpp::cCommBuffer *b, const RouteReply& obj) {obj.parsimPack(b);}
inline void doParsimUnpacking(omnetpp::cCommBuffer *b, RouteReply& obj) {obj.parsimUnpack(b);}


#endif // ifndef __ROUTEREPLY_M_H
