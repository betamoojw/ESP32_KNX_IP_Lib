#pragma once
enum { NETWORK_PROV_SCHEME_SOFTAP, NETWORK_PROV_SCHEME_HANDLER_NONE, NETWORK_PROV_SECURITY_1 };
struct ProvisioningStub {
    unsigned starts = 0;
    template<class... Args> void beginProvision(Args...) { ++starts; }
};
extern ProvisioningStub WiFiProv;
