#ifndef IBV_INTERFACE_HELPER_H
#define IBV_INTERFACE_HELPER_H
 
#include <string>
#include <stdint.h>
#include "ns3/application-container.h"
#include "ns3/node-container.h"
#include "ns3/object-factory.h"
#include "ns3/ipv4-address.h"
#include "ns3/ibv-interface.h"
namespace ns3 {
class IbvInterfaceHelper
{
public:
  IbvInterfaceHelper ();
  IbvInterfaceHelper (std::string segName, uint32_t segSize);
  // void SetAttribute (std::string name, const AttributeValue &value);
  ApplicationContainer Install (NodeContainer c);
  // void SetStartTime (Time start);
  // void SetStopTime (Time stop);
private:
  ObjectFactory m_factory;
  std::string m_segName;
  uint32_t m_segSize;
  Time m_start;
  Time m_stop;
  
};
}
#endif /* IBV_INTERFACE_HELPER_H */ 