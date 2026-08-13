# Cluster-internal routing guide (hand-finish in KiCad)

Already done in the board: GND/+5V inner planes (all power pads tied via-in-pad),
and the 52 AD5593R pin escapes (fanned to vias). Freerouting handles SET/ADC + the
global bus. What is left to hand-route is the per-channel cluster internals below.
Route order per channel: GATE, SNS (both stay inside the cluster), then OUT, FB (to the
op-amp), then DRAIN (FET -> flyback -> connector; make these 0.4mm, they carry SMA current).

## LIMB 1
- ch1:  GATE: Q101.1 - R113.1 - R112.2   |   SNS: Q101.2 - R115.1 - R111.2   |   OUT: U102.1 - R112.1 - C111.1 - R114.1   |   FB: U102.2 - R111.1 - C111.2   |   DRAIN: J101.1 - Q101.3 - D101.2
- ch2:  GATE: Q102.1 - R123.1 - R122.2   |   SNS: Q102.2 - R125.1 - R121.2   |   OUT: U102.7 - R122.1 - C121.1 - R124.1   |   FB: U102.6 - R121.1 - C121.2   |   DRAIN: J101.2 - Q102.3 - D102.2
- ch3:  GATE: Q103.1 - R133.1 - R132.2   |   SNS: Q103.2 - R135.1 - R131.2   |   OUT: U103.1 - R132.1 - C131.1 - R134.1   |   FB: U103.2 - R131.1 - C131.2   |   DRAIN: J101.3 - Q103.3 - D103.2
- ch4:  GATE: Q104.1 - R143.1 - R142.2   |   SNS: Q104.2 - R145.1 - R141.2   |   OUT: U103.7 - R142.1 - C141.1 - R144.1   |   FB: U103.6 - R141.1 - C141.2   |   DRAIN: J101.4 - Q104.3 - D104.2

## LIMB 2
- ch1:  GATE: Q201.1 - R213.1 - R212.2   |   SNS: Q201.2 - R215.1 - R211.2   |   OUT: U202.1 - R212.1 - C211.1 - R214.1   |   FB: U202.2 - R211.1 - C211.2   |   DRAIN: J201.1 - Q201.3 - D201.2
- ch2:  GATE: Q202.1 - R223.1 - R222.2   |   SNS: Q202.2 - R225.1 - R221.2   |   OUT: U202.7 - R222.1 - C221.1 - R224.1   |   FB: U202.6 - R221.1 - C221.2   |   DRAIN: J201.2 - Q202.3 - D202.2
- ch3:  GATE: Q203.1 - R233.1 - R232.2   |   SNS: Q203.2 - R235.1 - R231.2   |   OUT: U203.1 - R232.1 - C231.1 - R234.1   |   FB: U203.2 - R231.1 - C231.2   |   DRAIN: J201.3 - Q203.3 - D203.2
- ch4:  GATE: Q204.1 - R243.1 - R242.2   |   SNS: Q204.2 - R245.1 - R241.2   |   OUT: U203.7 - R242.1 - C241.1 - R244.1   |   FB: U203.6 - R241.1 - C241.2   |   DRAIN: J201.4 - Q204.3 - D204.2

## LIMB 3
- ch1:  GATE: Q301.1 - R313.1 - R312.2   |   SNS: Q301.2 - R315.1 - R311.2   |   OUT: U302.1 - R312.1 - C311.1 - R314.1   |   FB: U302.2 - R311.1 - C311.2   |   DRAIN: J301.1 - Q301.3 - D301.2
- ch2:  GATE: Q302.1 - R323.1 - R322.2   |   SNS: Q302.2 - R325.1 - R321.2   |   OUT: U302.7 - R322.1 - C321.1 - R324.1   |   FB: U302.6 - R321.1 - C321.2   |   DRAIN: J301.2 - Q302.3 - D302.2
- ch3:  GATE: Q303.1 - R333.1 - R332.2   |   SNS: Q303.2 - R335.1 - R331.2   |   OUT: U303.1 - R332.1 - C331.1 - R334.1   |   FB: U303.2 - R331.1 - C331.2   |   DRAIN: J301.3 - Q303.3 - D303.2
- ch4:  GATE: Q304.1 - R343.1 - R342.2   |   SNS: Q304.2 - R345.1 - R341.2   |   OUT: U303.7 - R342.1 - C341.1 - R344.1   |   FB: U303.6 - R341.1 - C341.2   |   DRAIN: J301.4 - Q304.3 - D304.2

## LIMB 4
- ch1:  GATE: Q401.1 - R413.1 - R412.2   |   SNS: Q401.2 - R415.1 - R411.2   |   OUT: U402.1 - R412.1 - C411.1 - R414.1   |   FB: U402.2 - R411.1 - C411.2   |   DRAIN: J401.1 - Q401.3 - D401.2
- ch2:  GATE: Q402.1 - R423.1 - R422.2   |   SNS: Q402.2 - R425.1 - R421.2   |   OUT: U402.7 - R422.1 - C421.1 - R424.1   |   FB: U402.6 - R421.1 - C421.2   |   DRAIN: J401.2 - Q402.3 - D402.2
- ch3:  GATE: Q403.1 - R433.1 - R432.2   |   SNS: Q403.2 - R435.1 - R431.2   |   OUT: U403.1 - R432.1 - C431.1 - R434.1   |   FB: U403.2 - R431.1 - C431.2   |   DRAIN: J401.3 - Q403.3 - D403.2
- ch4:  GATE: Q404.1 - R443.1 - R442.2   |   SNS: Q404.2 - R445.1 - R441.2   |   OUT: U403.7 - R442.1 - C441.1 - R444.1   |   FB: U403.6 - R441.1 - C441.2   |   DRAIN: J401.4 - Q404.3 - D404.2
