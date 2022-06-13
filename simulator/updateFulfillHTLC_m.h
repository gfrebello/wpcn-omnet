//
// Generated file, do not edit! Created by opp_msgtool 6.0 from updateFulfillHTLC.msg.
//

#ifndef __UPDATEFULFILLHTLC_M_H
#define __UPDATEFULFILLHTLC_M_H

#if defined(__clang__)
#  pragma clang diagnostic ignored "-Wreserved-id-macro"
#endif
#include <omnetpp.h>

// opp_msgtool version check
#define MSGC_VERSION 0x0600
#if (MSGC_VERSION!=OMNETPP_VERSION)
#    error Version mismatch! Probably this file was generated by an earlier version of opp_msgtool: 'make clean' should help.
#endif

class UpdateFulfillHTLC;
// cplusplus {{
    #include <string>
    #include "messages.h"
// }}

/**
 * Class generated from <tt>updateFulfillHTLC.msg:6</tt> by opp_msgtool.
 * <pre>
 * packet UpdateFulfillHTLC
 * {
 *     string htlcId;
 *     string paymentHash;
 *     string preImage;
 *     double value;
 * }
 * </pre>
 */
class UpdateFulfillHTLC : public ::omnetpp::cPacket
{
  protected:
    omnetpp::opp_string htlcId;
    omnetpp::opp_string paymentHash;
    omnetpp::opp_string preImage;
    double value = 0;

  private:
    void copy(const UpdateFulfillHTLC& other);

  protected:
    bool operator==(const UpdateFulfillHTLC&) = delete;

  public:
    UpdateFulfillHTLC(const char *name=nullptr, short kind=0);
    UpdateFulfillHTLC(const UpdateFulfillHTLC& other);
    virtual ~UpdateFulfillHTLC();
    UpdateFulfillHTLC& operator=(const UpdateFulfillHTLC& other);
    virtual UpdateFulfillHTLC *dup() const override {return new UpdateFulfillHTLC(*this);}
    virtual void parsimPack(omnetpp::cCommBuffer *b) const override;
    virtual void parsimUnpack(omnetpp::cCommBuffer *b) override;

    virtual const char * getHtlcId() const;
    virtual void setHtlcId(const char * htlcId);

    virtual const char * getPaymentHash() const;
    virtual void setPaymentHash(const char * paymentHash);

    virtual const char * getPreImage() const;
    virtual void setPreImage(const char * preImage);

    virtual double getValue() const;
    virtual void setValue(double value);
};

inline void doParsimPacking(omnetpp::cCommBuffer *b, const UpdateFulfillHTLC& obj) {obj.parsimPack(b);}
inline void doParsimUnpacking(omnetpp::cCommBuffer *b, UpdateFulfillHTLC& obj) {obj.parsimUnpack(b);}


namespace omnetpp {

template<> inline UpdateFulfillHTLC *fromAnyPtr(any_ptr ptr) { return check_and_cast<UpdateFulfillHTLC*>(ptr.get<cObject>()); }

}  // namespace omnetpp

#endif // ifndef __UPDATEFULFILLHTLC_M_H

