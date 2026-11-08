// What a host calls to make the plugin. The build gives each plugin its
// machine as DM_MACHINE, Console or Cassette.

#include "PluginProcessor.h"

#ifndef DM_MACHINE
#error "DM_MACHINE names the machine this plugin is, Console or Cassette"
#endif

juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() {
  return new dm::plugin::MachineProcessor(dm::ClipType::DM_MACHINE);
}
