# SPDX-License-Identifier: MIT

# WHY: Every union's first member is `none :Void`: an all-zero message decodes as it and is refused.
# Enum names avoid C++ macro names (ERROR, DEBUG): generated code is upper case; windows.h defines ERROR.

@0xfefab2165040fa61;

using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("bitoscope::ipc::wire::decoder");

struct DecoderFacetInfo {
  decoders @0 :List(DecoderInfo);
  pluginVersion @1 :Text;
  runsScripts @2 :Bool;
}

struct DecoderInfo {
  name @0 :Text;
  version @1 :Text;
  channels @2 :List(ChannelSpec);
  options @3 :List(OptionSpec);
  unsupportedOptions @4 :List(Text);
  annotationClasses @5 :List(AnnotationClass);
  rows @6 :List(Row);
  allowsSharedChannels @7 :Bool;
  outputs @8 :List(OutputSpec);
  inputs @9 :List(InputSpec);
  display @10 :Text;
  protocols @11 :List(Text);
  rowSources @12 :List(RowSource);
}

struct SourceRef {
  row @0 :UInt32;
  classes @1 :List(UInt32);
}

# WHY: BitGrid: t(j) = floor(j * R / B), R sample rate, B rate option; bit i spans [s + t(off+i), s + t(off+i+1)).
# Frame start s = b - t(offsetBits); bit order is top bit first when the order option reads "msb-first".
struct BitGrid {
  rateOption @0 :Text;
  offsetBits @1 :UInt32;
  countOption @2 :Text;
  orderOption @3 :Text;
}

struct ClockEdges {
  clockChannel @0 :Text;
  dataChannel @1 :Text;
  countOption @2 :Text;
  count @3 :UInt32;
}

struct RowSource {
  row @0 :UInt32;
  cls @1 :UInt32;
  source @2 :SourceRef;
  enableOption @3 :Text;
  rule :union {
    none @4 :Void;
    wordList @5 :Void;
    bitGrid @6 :BitGrid;
    clockEdges @7 :ClockEdges;
  }
}

enum StreamType {
  unspecified @0;
  words @1;
  spiTransfers @2;
}

struct OutputSpec {
  name @0 :Text;
  description @1 :Text;
  type @2 :StreamType;
  width @3 :UInt32;
  widthOption @4 :Text;
  requiresChannel @5 :Text;
  display @6 :Text;
  carries @7 :Text;
  carriesInput @8 :Text;
}

struct InputSpec {
  name @0 :Text;
  description @1 :Text;
  type @2 :StreamType;
  width @3 :UInt32;
  required @4 :Bool;
  display @5 :Text;
}

struct ChannelSpec {
  name @0 :Text;
  description @1 :Text;
  required @2 :Bool;
  display @3 :Text;
}

enum OptionType {
  unspecified @0;
  integer @1;
  string @2;
}

struct Limit {
  union {
    none @0 :Void;
    value @1 :Int64;
  }
}

struct OptionSpec {
  name @0 :Text;
  description @1 :Text;
  type @2 :OptionType;
  defaultValue @3 :Text;
  allowedValues @4 :List(Text);
  min @5 :Limit;
  max @6 :Limit;
  unsupportedValues @7 :List(Text);
  display @8 :Text;
}

struct ValueFormat {
  union {
    none @0 :Void;
    hex @1 :HexFormat;
    address7 @2 :Void;
    nOfM @3 :Void;
    decimal @4 :Void;
    nOfMBytes @5 :Void;
    bytes @6 :Void;
  }
}

struct HexFormat {
  minDigits @0 :UInt32;
  digitsOption @1 :Text;
}

# WHY: Role names carry `Role` because generated code spells them upper case and windows.h defines ERROR.
enum Role {
  noneRole @0;
  dataRole @1;
  startRole @2;
  stopRole @3;
  okRole @4;
  errorRole @5;
  controlRole @6;
  addressRole @7;
  bitRole @8;
  infoRole @9;
}

struct AnnotationClass {
  id @0 :UInt32;
  name @1 :Text;
  format @2 :ValueFormat;
  role @3 :Role;
}

struct Row {
  id @0 :UInt32;
  name @1 :Text;
}

struct ChannelBinding {
  name @0 :Text;
  channel @1 :UInt32;
}

struct OptionValue {
  name @0 :Text;
  value @1 :Text;
}

struct CaptureFacts {
  sampleRateMilliHz @0 :UInt64;
  channelCount @1 :UInt32;
  totalSamples @2 :UInt64;
  totalIsBound @3 :Bool;
}

struct InputBinding {
  name @0 :Text;
  type @1 :StreamType;
  width @2 :UInt32;
}

struct JobSetup {
  decoder @0 :Text;
  channels @1 :List(ChannelBinding);
  options @2 :List(OptionValue);
  facts @3 :CaptureFacts;
  outputs @4 :List(Text);
  inputs @5 :List(InputBinding);
  progressStep @6 :UInt64;
}

struct RingGeometry {
  slotCount @0 :UInt32;
  slotPayloadBytes @1 :UInt64;
}

struct DecodeStart {
  setup @0 :JobSetup;
  ringName @1 :Text;
  ring @2 :RingGeometry;
}

struct StreamItem {
  begin @0 :UInt64;
  end @1 :UInt64;
  value @2 :UInt64;
  kind @3 :UInt32;
  flags @4 :UInt32;
}

struct ItemFeed {
  input @0 :Text;
  items @1 :List(StreamItem);
  last @2 :Bool;
}

struct DecoderCommand {
  jobId @0 :UInt64;
  union {
    none @1 :Void;
    check @2 :JobSetup;
    decode @3 :DecodeStart;
    cancel @4 :Void;
    items @5 :ItemFeed;
  }
}

struct Annotation {
  begin @0 :UInt64;
  end @1 :UInt64;
  row @2 :UInt32;
  cls @3 :UInt32;
  value @4 :UInt64;
}

enum DecodeStatus {
  unspecified @0;
  completed @1;
  failure @2;
  cancelled @3;
}

struct DecodeEnded {
  status @0 :DecodeStatus;
  error @1 :Text;
  annotationCount @2 :UInt64;
  samplesRead @3 :UInt64;
  itemCount @4 :UInt64;
  itemsRead @5 :UInt64;
}

struct ItemBatch {
  output @0 :Text;
  items @1 :List(StreamItem);
}

struct ItemsWanted {
  input @0 :Text;
}

# WHY: settled is a field beside the union, not a member: an unknown member fails the job, a field is skipped.
struct DecoderEvent {
  jobId @0 :UInt64;
  union {
    none @1 :Void;
    annotations @2 :List(Annotation);
    decodeEnded @3 :DecodeEnded;
    items @4 :ItemBatch;
    itemsWanted @5 :ItemsWanted;
    byteValues @7 :List(Data);
  }
  settled @6 :UInt64;
}
