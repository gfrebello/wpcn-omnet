//
// Generated file, do not edit! Created by nedtool 5.7 from commitmentSigned.msg.
//

#ifndef __COMMITMENTSIGNED_M_H
#define __COMMITMENTSIGNED_M_H

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
    #include <vector>
    #include "HTLC.h"
    
    typedef std::vector<HTLC *> HTLCVector;
// }}

/**
 * Class generated from <tt>commitmentSigned.msg:25</tt> by nedtool.
 * <pre>
 * packet commitmentSigned
 * {
 *     HTLCVector HTLCs;
 *     int id;
 * }
 * </pre>
 */
class commitmentSigned : public ::omnetpp::cPacket
{
  protected:
    HTLCVector HTLCs;
    int id;

  private:
    void copy(const commitmentSigned& other);

  protected:
    // protected and unimplemented operator==(), to prevent accidental usage
    bool operator==(const commitmentSigned&);

  public:
    commitmentSigned(const char *name=nullptr, short kind=0);
    commitmentSigned(const commitmentSigned& other);
    virtual ~commitmentSigned();
    commitmentSigned& operator=(const commitmentSigned& other);
    virtual commitmentSigned *dup() const override {return new commitmentSigned(*this);}
    virtual void parsimPack(omnetpp::cCommBuffer *b) const override;
    virtual void parsimUnpack(omnetpp::cCommBuffer *b) override;

    // field getter/setter methods
    virtual HTLCVector& getHTLCs();
    virtual const HTLCVector& getHTLCs() const {return const_cast<commitmentSigned*>(this)->getHTLCs();}
    virtual void setHTLCs(const HTLCVector& HTLCs);
    virtual int getId() const;
    virtual void setId(int id);
};

inline void doParsimPacking(omnetpp::cCommBuffer *b, const commitmentSigned& obj) {obj.parsimPack(b);}
inline void doParsimUnpacking(omnetpp::cCommBuffer *b, commitmentSigned& obj) {obj.parsimUnpack(b);}


#endif // ifndef __COMMITMENTSIGNED_M_H

