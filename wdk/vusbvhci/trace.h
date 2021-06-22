#ifndef VUSBVHCI_TRACE_H
#define VUSBVHCI_TRACE_H

//
// Define the tracing flags.
//
// Tracing GUID - 608f5973-baba-4b78-9660-7d67b9974b92
//

#define WPP_CONTROL_GUIDS                                              \
    WPP_DEFINE_CONTROL_GUID(                                           \
        vusbvhciTraceGuid, (608f5973,baba,4b78,9660,7d67b9974b92),     \
                                                                       \
        WPP_DEFINE_BIT(TRACE_VUSBVHCI)                                 \
        WPP_DEFINE_BIT(TRACE_WMI)                                      \
        WPP_DEFINE_BIT(TRACE_POWER)                                    \
        WPP_DEFINE_BIT(TRACE_PNP)                                      \
        WPP_DEFINE_BIT(TRACE_BUSIF)                                    \
        WPP_DEFINE_BIT(TRACE_INTERNAL_IO)                              \
        WPP_DEFINE_BIT(TRACE_USER_IO)                                  \
        )

#define WPP_FLAG_LEVEL_LOGGER(flag, level)                                  \
    WPP_LEVEL_LOGGER(flag)

#define WPP_FLAG_LEVEL_ENABLED(flag, level)                                 \
    (WPP_LEVEL_ENABLED(flag) &&                                             \
     WPP_CONTROL(WPP_BIT_ ## flag).Level >= level)

#define WPP_LEVEL_FLAGS_LOGGER(lvl,flags) \
           WPP_LEVEL_LOGGER(flags)

#define WPP_LEVEL_FLAGS_ENABLED(lvl, flags) \
           (WPP_LEVEL_ENABLED(flags) && WPP_CONTROL(WPP_BIT_ ## flags).Level >= lvl)

//
// This comment block is scanned by the trace preprocessor to define our
// Trace function.
//
// begin_wpp config
// FUNC Trace{FLAG=TRACE_VUSBVHCI}(LEVEL, MSG, ...);
// FUNC TraceEvents(LEVEL, FLAGS, MSG, ...);
// end_wpp
//

#endif // !VUSBVHCI_TRACE_H