#pragma once
// Disabled: stock schematic has no battery-to-ADC measurement connection.
// Only enable after adding/verifying an external divider on a free ADC1 pin.
#ifndef BUS_BATTERY_ADC_PIN
#define BUS_BATTERY_ADC_PIN -1
#endif
#ifndef BUS_BATTERY_DIVIDER_RATIO
#define BUS_BATTERY_DIVIDER_RATIO 2.0f
#endif
#if BUS_BATTERY_ADC_PIN != -1 && BUS_BATTERY_ADC_PIN != 3 && BUS_BATTERY_ADC_PIN != 8
#error "Only unused ADC1 GPIO3 or GPIO8 allowed; verify physical accessibility and your board revision."
#endif
static_assert(BUS_BATTERY_DIVIDER_RATIO >= 1.5f && BUS_BATTERY_DIVIDER_RATIO <= 4.0f,
              "Invalid battery divider ratio");
