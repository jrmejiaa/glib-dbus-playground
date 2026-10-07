#include "unit-monitor.hpp"

#include <cstdio>
#include <cstdarg>

UnitMonitor::UnitMonitor(GDbusSystemd1Unit *pUnit, GDbusSystemd1Service *pService, const std::string &rUnitName, const std::string &rObjectPath)
    : m_pUnit(pUnit),
      m_pService(pService),
      m_UnitName(rUnitName),
      m_ObjectPath(rObjectPath)
{
    log("Constructing object with unit-proxy=%p and service-proxy=%p", pUnit, pService);
}

UnitMonitor::~UnitMonitor()
{
    log("Destructing object and freeing proxys");
    g_object_unref(m_pUnit);
    g_object_unref(m_pService);
}

void UnitMonitor::log(const char *pFormat, ...)
{
    std::fprintf(stdout, "    [UnitMonitor] ");

    std::va_list args;
    va_start(args, pFormat);
    std::vfprintf(stdout, pFormat, args);
    va_end(args);

    std::fprintf(stdout, "\n");
    std::fflush(stdout);
}