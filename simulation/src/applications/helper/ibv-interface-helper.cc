#include "ibv-interface-helper.h"
#include "ns3/uinteger.h"
#include "ns3/string.h"
#include "ns3/boolean.h"

namespace ns3 {
IbvInterfaceHelper::IbvInterfaceHelper()
{
  
}
IbvInterfaceHelper::IbvInterfaceHelper(std::string segName, uint32_t segSize)
  : m_segName(segName),
    m_segSize(segSize)
{
  m_factory.SetTypeId (IbvInterface::GetTypeId());
  m_factory.Set ("SegmentName", StringValue(segName));
  m_factory.Set ("SegmentSize", UintegerValue(segSize));
  
}

ApplicationContainer IbvInterfaceHelper::Install(NodeContainer c)
{
  ApplicationContainer apps;
  for (NodeContainer::Iterator i =c.Begin(); i != c.End(); ++i) {
    Ptr<Node> node = *i;
    Ptr<IbvInterface> app = m_factory.Create<IbvInterface>();
    node->AddApplication(app);
    apps.Add(app);
  }
  return apps;
}
}