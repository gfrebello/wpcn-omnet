//
// Generated file, do not edit! Created by opp_msgtool 6.0 from updateAddHTLC.msg.
//

#ifndef __UPDATEADDHTLC_M_H
#define __UPDATEADDHTLC_M_H

#if defined(__clang__)
#  pragma clang diagnostic ignored "-Wreserved-id-macro"
#endif
#include <omnetpp.h>

// opp_msgtool version check
#define MSGC_VERSION 0x0600
#if (MSGC_VERSION!=OMNETPP_VERSION)
#    error Version mismatch! Probably this file was generated by an earlier version of opp_msgtool: 'make clean' should help.
#endif

class UpdateAddHTLC;
// cplusplus {{
    #include <string>
    #include "messages.h"
// }}

/**
 * Class generated from <tt>updateAddHTLC.msg:6</tt> by opp_msgtool.
 * <pre>
 * packet UpdateAddHTLC
 * {
 *     string source;
 *     string htlcId;
 *     string paymentHash;
 *     simtime_t timeout;
 *     double value;
 * }
 * </pre>
 */
class UpdateAddHTLC : public ::omnetpp::cPacket
{
  protected:
    omnetpp::opp_string source;
    omnetpp::opp_string htlcId;
    omnetpp::opp_string paymentHash;
    omnetpp::simtime_t timeout = SIMTIME_ZERO;
    double value = 0;

  private:
    void copy(const UpdateAddHTLC& other);

  protected:
    bool operator==(const UpdateAddHTLC&) = delete;

  public:
    UpdateAddHTLC(const char *name=nullptr, short kind=0);
    UpdateAddHTLC(const UpdateAddHTLC& other);
    virtual ~UpdateAddHTLC();
    UpdateAddHTLC& operator=(const UpdateAddHTLC& other);
    virtual UpdateAddHTLC *dup() const override {return new UpdateAddHTLC(*this);}
    virtual void parsimPack(omnetpp::cCommBuffer *b) const override;
    virtual void parsimUnpack(omnetpp::cCommBuffer *b) override;

    virtual const char * getSource() const;
    virtual void setSource(const char * source);

    virtual const char * getHtlcId() const;
    virtual void setHtlcId(const char * htlcId);

    virtual const char * getPaymentHash() const;
    virtual void setPaymentHash(const char * paymentHash);

    virtual omnetpp::simtime_t getTimeout() const;
    virtual void setTimeout(omnetpp::simtime_t timeout);

    virtual double getValue() const;
    virtual void setValue(double value);
};

inline void doParsimPacking(omnetpp::cCommBuffer *b, const UpdateAddHTLC& obj) {obj.parsimPack(b);}
inline void doParsimUnpacking(omnetpp::cCommBuffer *b, UpdateAddHTLC& obj) {obj.parsimUnpack(b);}


namespace omnetpp {

template<> inline UpdateAddHTLC *fromAnyPtr(any_ptr ptr) { return check_and_cast<UpdateAddHTLC*>(ptr.get<cObject>()); }

}  // namespace omnetpp

#endif // ifndef __UPDATEADDHTLC_M_H

