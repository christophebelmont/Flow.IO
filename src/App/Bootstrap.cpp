#include "App/Bootstrap.h"

#include "App/BuildFlags.h"

#if FLOW_BUILD_IS_FLOWIO
#include "Profiles/FlowIO/FlowIOProfile.h"
#endif

#if FLOW_BUILD_IS_SUPERVISOR
#include "Profiles/Supervisor/SupervisorProfile.h"
#endif

namespace {

AppContext gContext{};
bool gStarted = false;

const FirmwareProfile& resolveProfile()
{
#if FLOW_BUILD_IS_FLOWIO
    return Profiles::FlowIO::profile();
#elif FLOW_BUILD_IS_SUPERVISOR
    return Profiles::Supervisor::profile();
#else
#error "Unsupported build profile."
#endif
}

}  // namespace

namespace Bootstrap {

void run()
{
    if (gStarted) return;

    const FirmwareProfile& profile = resolveProfile();
    gContext.profile = &profile;
    gContext.board = profile.board;
    gContext.domain = profile.domain;
    gContext.identity = &profile.identity;
    gContext.supervisorRuntime = profile.supervisorRuntime;

    if (profile.setup) {
        profile.setup(gContext);
    }

    gContext.bootCompleted = true;
    gStarted = true;
}

void loop()
{
    if (!gStarted) {
        run();
    }

    (void)gContext.moduleManager.tickStartup(gContext.registry, gContext.services);

    if (gContext.profile && gContext.profile->loop) {
        gContext.profile->loop(gContext);
    }
}

AppContext& context()
{
    return gContext;
}

const FirmwareProfile& activeProfile()
{
    return resolveProfile();
}

}  // namespace Bootstrap
