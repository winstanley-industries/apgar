#include "apgar/benchmark/phase4_per_net_report_artifact.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <exception>
#include <new>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "apgar/board_ir/stable_hash.h"
#include "src/allocator/negotiated_prices_internal.h"
#include "src/benchmark/phase4_paired_trial_internal.h"

namespace apgar::benchmark {
namespace {

constexpr std::uint64_t kRepresentativeManifestChecksumV2 = 9613362670139358355ULL;

constexpr std::array<Phase4WorkloadNetRosterManifestEntryV1, 38> kRosterManifestV1 = {{
    {1, 1, 7311872938254494931ULL, 100, 8019315640326555851ULL, 17177310953492740304ULL,
     13282114147341482240ULL, 5538372392994751246ULL, 6, 2173846823820267664ULL},
    {1, 1, 7311872938254494931ULL, 101, 6282934470436762953ULL, 8332006487454784915ULL,
     14042041371988697829ULL, 10473424255958299978ULL, 6, 516507956444489275ULL},
    {1, 1, 7311872938254494931ULL, 102, 15237650574036737121ULL, 3673964883945221241ULL,
     294114957451251316ULL, 17197223538998915196ULL, 6, 1906661734731921340ULL},
    {1, 1, 7311872938254494931ULL, 200, 7606926012288823053ULL, 8961918205590033051ULL,
     18156786999259275280ULL, 4293082499256761742ULL, 64, 419512731498974382ULL},
    {1, 1, 7311872938254494931ULL, 201, 6785363568674608751ULL, 8290434967958350891ULL,
     16580444215032052139ULL, 14318665507042836911ULL, 64, 2662622686238414562ULL},
    {1, 1, 7311872938254494931ULL, 210, 16619045146680576462ULL, 12855252495554429399ULL,
     2126535844776657388ULL, 3713609457646610243ULL, 64, 9751878321361620083ULL},
    {1, 1, 7311872938254494931ULL, 211, 4307496802892609496ULL, 3215683804316488459ULL,
     5324204954166205661ULL, 3670615384074780259ULL, 64, 3478753708828863814ULL},
    {1, 1, 7311872938254494931ULL, 220, 2412286233731448079ULL, 13670779733810538897ULL,
     2110350543768927159ULL, 13420664728743465954ULL, 64, 4250918245788022741ULL},
    {1, 1, 7311872938254494931ULL, 221, 7711941648473078637ULL, 1589587861854612041ULL,
     6389930286830547955ULL, 2126376047453183894ULL, 64, 2017094911743021420ULL},
    {1, 1, 7311872938254494931ULL, 1000, 15168059261654682115ULL, 12455736942815589650ULL,
     5242796936923564672ULL, 11844826437413719781ULL, 256, 2458501662857965944ULL},
    {1, 1, 7311872938254494931ULL, 1001, 17982047200241829329ULL, 10479311042317067495ULL,
     2755155984875167426ULL, 13255178046909160804ULL, 256, 6223858218000252411ULL},
    {1, 1, 7311872938254494931ULL, 1002, 12842072968640087763ULL, 5018824197467248965ULL,
     11442899055977189637ULL, 18019744668043937976ULL, 256, 12080041490307691230ULL},
    {1, 1, 7311872938254494931ULL, 1003, 748221261688208573ULL, 407578234817831742ULL,
     13155395501062140320ULL, 7200197406149591279ULL, 256, 11944628289031026033ULL},
    {1, 1, 7311872938254494931ULL, 1004, 7809530797303947283ULL, 9401139927482655634ULL,
     1524675911170485492ULL, 14421301855177718022ULL, 256, 10779261535567137256ULL},
    {1, 1, 7311872938254494931ULL, 1005, 11369748036396858849ULL, 17888093242720644632ULL,
     12325461504812003102ULL, 7271636459800995303ULL, 256, 5666450171756600642ULL},
    {1, 1, 7311872938254494931ULL, 1006, 16382179578884687203ULL, 8060967223190264604ULL,
     7581400372286725387ULL, 16218065700224854155ULL, 256, 9701705201029810944ULL},
    {1, 1, 7311872938254494931ULL, 1007, 5578087683802520485ULL, 8075005746480077158ULL,
     5948745412546040827ULL, 3924313281755579348ULL, 256, 12047169472540538734ULL},
    {1, 1, 7311872938254494931ULL, 1100, 17064449010472703203ULL, 11666200509965728269ULL,
     17134656231817402273ULL, 3786412522395503665ULL, 256, 9897084392701108457ULL},
    {1, 1, 7311872938254494931ULL, 1101, 11347823812296864429ULL, 18373313919931784222ULL,
     17322784867041215835ULL, 9022306082554262192ULL, 256, 4128961349663751931ULL},
    {1, 1, 7311872938254494931ULL, 1102, 17509199292925069155ULL, 17059479164677002296ULL,
     18304766564097410464ULL, 13942885604412409191ULL, 256, 3542750862433612680ULL},
    {1, 1, 7311872938254494931ULL, 1103, 10735519199761686321ULL, 17535462790510929520ULL,
     9909785357976259742ULL, 82693694040783835ULL, 256, 8596027220702023380ULL},
    {1, 1, 7311872938254494931ULL, 1104, 13840894844114907611ULL, 1020058174148458799ULL,
     15687118578088735378ULL, 11489452317299317169ULL, 256, 16702166783134819124ULL},
    {1, 1, 7311872938254494931ULL, 1105, 18428681881191374613ULL, 3993889398419491956ULL,
     12869412038977548431ULL, 12149479967161297196ULL, 256, 16592278988740295096ULL},
    {1, 1, 7311872938254494931ULL, 1106, 13539415826061509211ULL, 9085952374324761332ULL,
     12943634988578891876ULL, 4968484081199086216ULL, 256, 15937486556150261043ULL},
    {1, 1, 7311872938254494931ULL, 1107, 3263168350504626049ULL, 14780961431828150564ULL,
     5012535039376234415ULL, 15194474104101585110ULL, 256, 14164881730761554891ULL},
    {1, 1, 7311872938254494931ULL, 1200, 11391728102114583562ULL, 4291914302087681457ULL,
     3069707195806466886ULL, 14308262233143586696ULL, 384, 5445251112899389343ULL},
    {1, 1, 7311872938254494931ULL, 1201, 9940811621395116444ULL, 14732858144068916682ULL,
     5812791566319473725ULL, 4907120376530011258ULL, 384, 124395896088615069ULL},
    {1, 1, 7311872938254494931ULL, 1202, 1270559019391015762ULL, 5435983105874565857ULL,
     11026168635610540618ULL, 6008149509503254103ULL, 384, 7427556956026430453ULL},
    {1, 1, 7311872938254494931ULL, 1203, 11535097256105186392ULL, 15584788893265887959ULL,
     4323709710907421020ULL, 11794516539356494444ULL, 384, 13569753529841482838ULL},
    {1, 1, 7311872938254494931ULL, 1204, 14902389270997626626ULL, 18393404450822503490ULL,
     11783418953922311443ULL, 9759632228935670188ULL, 384, 8566051889608324663ULL},
    {1, 1, 7311872938254494931ULL, 1205, 12200879459693019140ULL, 7559990405444721342ULL,
     14008400430775752403ULL, 13043652902045473560ULL, 384, 14034212745676592583ULL},
    {1, 1, 7311872938254494931ULL, 1206, 7266717595463850122ULL, 1868580510958414160ULL,
     15998466354794152653ULL, 1344756893386604809ULL, 384, 10574142714405812379ULL},
    {1, 1, 7311872938254494931ULL, 1207, 1743725769046491000ULL, 10865074259813806884ULL,
     9982708680592526864ULL, 17735713516056715266ULL, 384, 6227893697939889116ULL},
    {1, 1, 7311872938254494931ULL, 2002, 9392411121646056775ULL, 401798758506216684ULL,
     8150387804082603438ULL, 8197710098198292177ULL, 256, 18294705036607505349ULL},
    {1, 1, 7311872938254494931ULL, 2003, 10053408626838512557ULL, 2702793700547040335ULL,
     208943022645105843ULL, 10130765989693505706ULL, 128, 6274545081644436584ULL},
    {1, 1, 7311872938254494931ULL, 2004, 10180029229858426087ULL, 4689925058819716281ULL,
     3982744825193714409ULL, 4906443334608893765ULL, 64, 2571412218883529745ULL},
    {1, 1, 7311872938254494931ULL, 3000, 3990076980468972638ULL, 2514215044260473785ULL,
     2594817663601316769ULL, 5579730177899030416ULL, 1024, 10319772584150400972ULL},
    {1, 1, 7311872938254494931ULL, 4000, 7587879808058813459ULL, 7603876739078231632ULL,
     229027575659763193ULL, 13031419002947588674ULL, 2, 16396043058073973332ULL},
}};

static_assert(std::ranges::is_sorted(kRosterManifestV1, {},
                                     &Phase4WorkloadNetRosterManifestEntryV1::case_id));

constexpr std::array<Phase4WorkloadNetRosterManifestExclusionV1, 4> kRosterExclusionsV1 = {{
    {2000, 8203613321943675931ULL,
     Phase4WorkloadNetRosterExclusionDispositionV1::kDescriptorOnlyUnsupportedPool},
    {2001, 11000562598404360444ULL,
     Phase4WorkloadNetRosterExclusionDispositionV1::kDescriptorOnlyUnsupportedPool},
    {3001, 5505549972392664092ULL,
     Phase4WorkloadNetRosterExclusionDispositionV1::kCompiledWorkBound},
    {3002, 6801270323093200014ULL,
     Phase4WorkloadNetRosterExclusionDispositionV1::kCompiledWorkBound},
}};

static_assert(std::ranges::is_sorted(kRosterExclusionsV1, {},
                                     &Phase4WorkloadNetRosterManifestExclusionV1::case_id));

constexpr std::array<Phase4WorkloadNetRosterManifestEntryV1, 38> kRosterManifestV2 = {{
    {2, 2, 4182833841936446798ULL, 10100, 10228607137210506848ULL, 3217116157854000498ULL,
     3708260191876229468ULL, 6027624155096895757ULL, 6, 12521697377381992336ULL},
    {2, 2, 4182833841936446798ULL, 10101, 12534649919415282514ULL, 10937201691349586908ULL,
     17722123837174620426ULL, 4158717984138887175ULL, 6, 18050473376725214649ULL},
    {2, 2, 4182833841936446798ULL, 10102, 9108441457058720974ULL, 13971517485574820650ULL,
     7458776459072423372ULL, 7643361195091441726ULL, 6, 6293759481185660505ULL},
    {2, 2, 4182833841936446798ULL, 10200, 10695636458714729567ULL, 6576364998077639914ULL,
     2447570761459181156ULL, 10842289401720827444ULL, 64, 718781758134362332ULL},
    {2, 2, 4182833841936446798ULL, 10201, 4780362575974240017ULL, 1170585785749259975ULL,
     15518679849240237846ULL, 3989073419330904360ULL, 64, 17918408665925418079ULL},
    {2, 2, 4182833841936446798ULL, 10210, 111802236827638924ULL, 13032949222693167053ULL,
     8278042861691986003ULL, 3446124616876093946ULL, 64, 6951598089694699218ULL},
    {2, 2, 4182833841936446798ULL, 10211, 10072227360595614054ULL, 9800416471022572180ULL,
     9313197537736512173ULL, 16626550182460378230ULL, 64, 7800368394898395973ULL},
    {2, 2, 4182833841936446798ULL, 10220, 17011582647336855521ULL, 4534216547630514888ULL,
     12640497438328008878ULL, 4780456240743882496ULL, 64, 7733114666958053561ULL},
    {2, 2, 4182833841936446798ULL, 10221, 13841678036039008815ULL, 10064164847344700412ULL,
     2302777592651024835ULL, 4301325213103002033ULL, 64, 4802867216703793533ULL},
    {2, 2, 4182833841936446798ULL, 11000, 8136738652820056141ULL, 2476998220201041869ULL,
     12702879829538190842ULL, 8510111773791366646ULL, 256, 16918227780685315839ULL},
    {2, 2, 4182833841936446798ULL, 11001, 9025649207859729135ULL, 129868894109733195ULL,
     3123002483948176728ULL, 1576204168038199387ULL, 256, 14412867192141928858ULL},
    {2, 2, 4182833841936446798ULL, 11002, 13758157734185782053ULL, 1530312207837069639ULL,
     6248092518556667216ULL, 17576602136241997304ULL, 256, 8830031394465138765ULL},
    {2, 2, 4182833841936446798ULL, 11003, 16832999589922620843ULL, 5799482920864064809ULL,
     5670575717578553304ULL, 17047459640325115981ULL, 256, 15096773894735607282ULL},
    {2, 2, 4182833841936446798ULL, 11004, 10868572533173993653ULL, 1233087454689931747ULL,
     15424045188600345080ULL, 12100770952801776407ULL, 256, 5077236833437032980ULL},
    {2, 2, 4182833841936446798ULL, 11005, 1902948818349247439ULL, 15106108367413670798ULL,
     9000808363812532116ULL, 10251624731676413774ULL, 256, 4669199570255553059ULL},
    {2, 2, 4182833841936446798ULL, 11006, 14987565108784072333ULL, 6290441341249542899ULL,
     14648590007385544376ULL, 3409681017975767848ULL, 256, 16812066801646357250ULL},
    {2, 2, 4182833841936446798ULL, 11007, 13846435945966906755ULL, 4308197026110786758ULL,
     6886586962757537789ULL, 14693638917351838544ULL, 256, 18282971630594186900ULL},
    {2, 2, 4182833841936446798ULL, 11100, 17254115015626694131ULL, 13621892480483243824ULL,
     3985171703181687743ULL, 14455106541564489145ULL, 256, 3482086558380620987ULL},
    {2, 2, 4182833841936446798ULL, 11101, 4631353101696368957ULL, 7471852269647370228ULL,
     14376871709671562414ULL, 11840714331748229763ULL, 256, 8953413426480224133ULL},
    {2, 2, 4182833841936446798ULL, 11102, 3443187958812335243ULL, 13348361695802709029ULL,
     16588744157530176849ULL, 680905808581787283ULL, 256, 8761640217038669334ULL},
    {2, 2, 4182833841936446798ULL, 11103, 14776480158817206729ULL, 11628233568003636837ULL,
     7992812382463676402ULL, 16884786860624758635ULL, 256, 1835207716877508965ULL},
    {2, 2, 4182833841936446798ULL, 11104, 3976063945855671043ULL, 10729950693401478992ULL,
     12242098821034020177ULL, 6679217322588658589ULL, 256, 2485016882148461024ULL},
    {2, 2, 4182833841936446798ULL, 11105, 4731964912821905621ULL, 5674762623871453036ULL,
     11601282020973814231ULL, 4308541346835106773ULL, 256, 16258231127987294347ULL},
    {2, 2, 4182833841936446798ULL, 11106, 16736978585831354331ULL, 14892584494879265677ULL,
     6504346041280847357ULL, 17227159938979966435ULL, 256, 9118379732650837183ULL},
    {2, 2, 4182833841936446798ULL, 11107, 9644762493109187945ULL, 463526743657728179ULL,
     9320781362191743678ULL, 2921575187413957010ULL, 256, 12197584162667887020ULL},
    {2, 2, 4182833841936446798ULL, 11200, 14558992864330598942ULL, 1232058813546457113ULL,
     1503912220307690516ULL, 11587849255170943588ULL, 384, 5670614489053562719ULL},
    {2, 2, 4182833841936446798ULL, 11201, 10136742974126937952ULL, 14173256805288273683ULL,
     1495139329132544806ULL, 13493282191036454925ULL, 384, 8788856667736241220ULL},
    {2, 2, 4182833841936446798ULL, 11202, 18189296475219763950ULL, 6628852326349369235ULL,
     14031082160115185972ULL, 16618138946565028855ULL, 384, 16807498195603833901ULL},
    {2, 2, 4182833841936446798ULL, 11203, 8447781850993000772ULL, 10693237650635755086ULL,
     1657536874448428646ULL, 17875661166905089009ULL, 384, 13832576674990431244ULL},
    {2, 2, 4182833841936446798ULL, 11204, 10894892895167280414ULL, 5587891652191391132ULL,
     8401502098282009686ULL, 14770658607695601322ULL, 384, 5925116735152085183ULL},
    {2, 2, 4182833841936446798ULL, 11205, 7101705348508213944ULL, 9229551299961563159ULL,
     9729733382135666456ULL, 5645702921802132111ULL, 384, 15887507704133815439ULL},
    {2, 2, 4182833841936446798ULL, 11206, 10374464461943108110ULL, 712616556097066391ULL,
     10426409899536900395ULL, 757659926964692250ULL, 384, 3930625192130737550ULL},
    {2, 2, 4182833841936446798ULL, 11207, 796307798496737908ULL, 1495326321299319594ULL,
     10517392412180594019ULL, 11928634378035431034ULL, 384, 1034197301085994999ULL},
    {2, 2, 4182833841936446798ULL, 12002, 15456014750933644988ULL, 18138895948317835272ULL,
     7146360669030614799ULL, 17750264605297057285ULL, 256, 5935305127240549430ULL},
    {2, 2, 4182833841936446798ULL, 12003, 592418894459475426ULL, 12078035219740956236ULL,
     17419536203139389805ULL, 18370558243653545970ULL, 128, 11256367023368876454ULL},
    {2, 2, 4182833841936446798ULL, 12004, 8094371739003837108ULL, 15927124833554587233ULL,
     9745345286114463611ULL, 5466651941191899182ULL, 64, 17138882257473826962ULL},
    {2, 2, 4182833841936446798ULL, 13000, 5672749310222075805ULL, 11481720930702911221ULL,
     18436758006885866983ULL, 6659276508905187440ULL, 1024, 1115153633669017100ULL},
    {2, 2, 4182833841936446798ULL, 14000, 10588276323446831186ULL, 4993717029123835713ULL,
     229027575659763193ULL, 13031419002947588674ULL, 2, 9966342868999091695ULL},
}};

static_assert(std::ranges::is_sorted(kRosterManifestV2, {},
                                     &Phase4WorkloadNetRosterManifestEntryV1::case_id));

constexpr std::array<Phase4WorkloadNetRosterManifestExclusionV1, 4> kRosterExclusionsV2 = {{
    {12000, 10463951918282411440ULL,
     Phase4WorkloadNetRosterExclusionDispositionV1::kDescriptorOnlyUnsupportedPool},
    {12001, 18273953003518375579ULL,
     Phase4WorkloadNetRosterExclusionDispositionV1::kDescriptorOnlyUnsupportedPool},
    {13001, 8122399637670938771ULL,
     Phase4WorkloadNetRosterExclusionDispositionV1::kCompiledWorkBound},
    {13002, 15811805131329987573ULL,
     Phase4WorkloadNetRosterExclusionDispositionV1::kCompiledWorkBound},
}};

static_assert(std::ranges::is_sorted(kRosterExclusionsV2, {},
                                     &Phase4WorkloadNetRosterManifestExclusionV1::case_id));

[[nodiscard]] Phase4PerNetReportArtifactError Error(std::string_view invariant,
                                                    std::string_view detail) {
  return Phase4PerNetReportArtifactError{.invariant_id = std::string(invariant),
                                         .detail = std::string(detail)};
}

[[nodiscard]] bool IsLowerHexCommit(std::string_view commit) noexcept {
  return commit.size() == 40 && std::ranges::all_of(commit, [](char character) {
           return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
         });
}

void HashExternalBudget(board_ir::StableHashBuilder* hash, const Phase4ExternalBudget& budget) {
  hash->AddU64(budget.maximum_prepared_elapsed_nanoseconds);
  hash->AddU64(budget.maximum_cold_elapsed_nanoseconds);
  hash->AddU64(budget.maximum_address_space_bytes);
  hash->AddU64(budget.maximum_peak_host_bytes);
}

void HashCorpusLimits(board_ir::StableHashBuilder* hash,
                      const Phase4RepresentativeCorpusLimits& limits) {
  hash->AddU64(limits.maximum_nets);
  hash->AddU64(limits.maximum_compiled_nodes);
  hash->AddU64(limits.maximum_compiled_host_bytes);
  hash->AddU64(limits.maximum_active_regions);
  hash->AddU64(limits.maximum_board_entities);
}

void HashCellConfig(board_ir::StableHashBuilder* hash, const Phase4CanonicalCellConfig& config) {
  hash->AddU32(config.schema_version);
  hash->AddU32(config.case_id);
  hash->AddU32(config.requested_pool_size);
  hash->AddU32(config.preparation_worker_count);
  hash->AddU32(config.repetitions);
  hash->AddU64(config.maximum_setup_elapsed_nanoseconds);
  HashExternalBudget(hash, config.external_budget);
  HashCorpusLimits(hash, config.corpus_limits);
}

[[nodiscard]] bool SameSemanticConfig(const Phase4TrialArmSemantics& semantics, Phase4TrialArm arm,
                                      const Phase4PairedTrialSpec& spec,
                                      const Phase4RepresentativeCase& representative_case,
                                      std::uint64_t expected_budget_checksum,
                                      bool corpus_v2) noexcept {
  const Phase4CaseDescriptor& descriptor = representative_case.descriptor;
  const std::uint32_t corpus_version =
      corpus_v2 ? kPhase4RepresentativeCorpusVersionV2 : kPhase4RepresentativeCorpusVersion;
  const std::uint64_t corpus_checksum =
      corpus_v2 ? Phase4RepresentativeCorpusChecksumV2() : Phase4RepresentativeCorpusChecksumV1();
  const std::uint64_t descriptor_fingerprint = corpus_v2
                                                   ? FingerprintPhase4CaseDescriptorV2(descriptor)
                                                   : FingerprintPhase4CaseDescriptorV1(descriptor);
  const std::uint64_t capacity_model_checksum =
      allocator::internal::ComputeResourceCapacityModelChecksumV1(
          allocator::internal::ResourceCapacityChecksumHeaderV1{
              .schema_version = representative_case.capacities.schema_version(),
              .associations = representative_case.capacities.associations(),
              .default_capacity_units = representative_case.capacities.default_capacity_units(),
          },
          representative_case.capacities.overrides());
  return semantics.schema_version == kPhase4PairedTrialSchemaVersion && semantics.arm == arm &&
         semantics.execution_order == Phase4TrialOrder::kBaselineFirst &&
         semantics.corpus_version == corpus_version &&
         semantics.corpus_checksum == corpus_checksum && semantics.case_id == spec.case_id &&
         semantics.descriptor_fingerprint == descriptor_fingerprint &&
         semantics.case_checksum == representative_case.case_checksum &&
         semantics.board_content_hash == representative_case.board.content_hash() &&
         semantics.workload_checksum == representative_case.workload.workload_checksum() &&
         semantics.capacity_model_checksum == capacity_model_checksum &&
         semantics.budget_checksum == expected_budget_checksum &&
         semantics.workload_net_count == representative_case.workload.nets().size() &&
         semantics.requested_pool_size == spec.requested_pool_size &&
         semantics.repetition_index == 0 && semantics.root_seed == spec.root_seed &&
         semantics.preparation_worker_count == spec.preparation_worker_count &&
         semantics.baseline_sweeps == spec.baseline_config.maximum_sweeps &&
         semantics.candidate_regeneration_epochs ==
             spec.candidate_session_config.maximum_regeneration_epochs &&
         semantics.candidate_columns_per_epoch ==
             spec.candidate_session_config.regeneration_plan_config.maximum_total_columns &&
         semantics.candidate_terminal_selection_rounds ==
             spec.candidate_session_config.schedules.back().maximum_selection_rounds &&
         semantics.external_budget == spec.external_budget &&
         semantics.opportunity.route_queries == spec.baseline_config.limits.maximum_route_queries &&
         semantics.opportunity.route_work_units ==
             spec.baseline_config.limits.maximum_total_route_work_units &&
         semantics.semantic_checksum != 0 &&
         semantics.semantic_checksum ==
             internal::ComputePhase4TrialArmSemanticChecksumV1(semantics);
}

class JsonWriter {
 public:
  void BeginObject() {
    output_.push_back('{');
    scopes_.push_back(true);
  }
  void EndObject() {
    output_.push_back('}');
    scopes_.pop_back();
  }
  void BeginArray() {
    output_.push_back('[');
    scopes_.push_back(true);
  }
  void EndArray() {
    output_.push_back(']');
    scopes_.pop_back();
  }
  void Key(std::string_view key) {
    Separate();
    String(key);
    output_.push_back(':');
  }
  void Element() { Separate(); }
  void String(std::string_view value) {
    constexpr char kHex[] = "0123456789abcdef";
    output_.push_back('"');
    for (const unsigned char character : value) {
      switch (character) {
        case '"':
          output_ += "\\\"";
          break;
        case '\\':
          output_ += "\\\\";
          break;
        case '\b':
          output_ += "\\b";
          break;
        case '\f':
          output_ += "\\f";
          break;
        case '\n':
          output_ += "\\n";
          break;
        case '\r':
          output_ += "\\r";
          break;
        case '\t':
          output_ += "\\t";
          break;
        default:
          if (character < 0x20U) {
            output_ += "\\u00";
            output_.push_back(kHex[(character >> 4U) & 0x0fU]);
            output_.push_back(kHex[character & 0x0fU]);
          } else {
            output_.push_back(static_cast<char>(character));
          }
      }
    }
    output_.push_back('"');
  }
  template <typename IntegerType>
  void Integer(IntegerType value) {
    static_assert(std::is_integral_v<IntegerType>);
    output_ += std::to_string(value);
  }
  template <typename Enum>
  void EnumValue(Enum value) {
    static_assert(std::is_enum_v<Enum>);
    Integer(static_cast<std::underlying_type_t<Enum>>(value));
  }
  void Bool(bool value) { output_ += value ? "true" : "false"; }
  void Null() { output_ += "null"; }
  [[nodiscard]] std::string Finish() && {
    output_.push_back('\n');
    return std::move(output_);
  }

 private:
  void Separate() {
    if (!scopes_.back()) {
      output_.push_back(',');
    }
    scopes_.back() = false;
  }
  std::string output_;
  std::vector<bool> scopes_;
};

void WriteExternalBudget(JsonWriter* writer, const Phase4ExternalBudget& budget) {
  writer->BeginObject();
  writer->Key("maximum_prepared_elapsed_nanoseconds");
  writer->Integer(budget.maximum_prepared_elapsed_nanoseconds);
  writer->Key("maximum_cold_elapsed_nanoseconds");
  writer->Integer(budget.maximum_cold_elapsed_nanoseconds);
  writer->Key("maximum_address_space_bytes");
  writer->Integer(budget.maximum_address_space_bytes);
  writer->Key("maximum_peak_host_bytes");
  writer->Integer(budget.maximum_peak_host_bytes);
  writer->EndObject();
}

void WriteCorpusLimits(JsonWriter* writer, const Phase4RepresentativeCorpusLimits& limits) {
  writer->BeginObject();
  writer->Key("maximum_nets");
  writer->Integer(limits.maximum_nets);
  writer->Key("maximum_compiled_nodes");
  writer->Integer(limits.maximum_compiled_nodes);
  writer->Key("maximum_compiled_host_bytes");
  writer->Integer(limits.maximum_compiled_host_bytes);
  writer->Key("maximum_active_regions");
  writer->Integer(limits.maximum_active_regions);
  writer->Key("maximum_board_entities");
  writer->Integer(limits.maximum_board_entities);
  writer->EndObject();
}

void WriteCellConfig(JsonWriter* writer, const Phase4CanonicalCellConfig& config) {
  writer->BeginObject();
  writer->Key("schema_version");
  writer->Integer(config.schema_version);
  writer->Key("case_id");
  writer->Integer(config.case_id);
  writer->Key("requested_pool_size");
  writer->Integer(config.requested_pool_size);
  writer->Key("preparation_worker_count");
  writer->Integer(config.preparation_worker_count);
  writer->Key("repetitions");
  writer->Integer(config.repetitions);
  writer->Key("maximum_setup_elapsed_nanoseconds");
  writer->Integer(config.maximum_setup_elapsed_nanoseconds);
  writer->Key("external_budget");
  WriteExternalBudget(writer, config.external_budget);
  writer->Key("corpus_limits");
  WriteCorpusLimits(writer, config.corpus_limits);
  writer->EndObject();
}

void WriteRouteOpportunity(JsonWriter* writer, const Phase4RouteOpportunity& opportunity) {
  writer->BeginObject();
  writer->Key("route_queries");
  writer->Integer(opportunity.route_queries);
  writer->Key("route_work_units");
  writer->Integer(opportunity.route_work_units);
  writer->EndObject();
}

void WriteBoardOutcome(JsonWriter* writer, const Phase4BoardOutcome& outcome) {
  writer->BeginObject();
  writer->Key("selected_net_count");
  writer->Integer(outcome.selected_net_count);
  writer->Key("no_candidate_net_count");
  writer->Integer(outcome.no_candidate_net_count);
  writer->Key("overused_resource_count");
  writer->Integer(outcome.overused_resource_count);
  writer->Key("total_overuse_units");
  writer->Integer(outcome.total_overuse_units);
  writer->Key("total_intrinsic_cost");
  writer->Integer(outcome.total_intrinsic_cost);
  writer->Key("world_checksum");
  writer->Integer(outcome.world_checksum);
  writer->EndObject();
}

void WriteArmSemantics(JsonWriter* writer, const Phase4TrialArmSemantics& value) {
  writer->BeginObject();
  writer->Key("schema_version");
  writer->Integer(value.schema_version);
  writer->Key("arm");
  writer->EnumValue(value.arm);
  writer->Key("execution_order");
  writer->EnumValue(value.execution_order);
  writer->Key("corpus_version");
  writer->Integer(value.corpus_version);
  writer->Key("corpus_checksum");
  writer->Integer(value.corpus_checksum);
  writer->Key("case_id");
  writer->Integer(value.case_id);
  writer->Key("descriptor_fingerprint");
  writer->Integer(value.descriptor_fingerprint);
  writer->Key("case_checksum");
  writer->Integer(value.case_checksum);
  writer->Key("board_content_hash");
  writer->Integer(value.board_content_hash);
  writer->Key("workload_checksum");
  writer->Integer(value.workload_checksum);
  writer->Key("capacity_model_checksum");
  writer->Integer(value.capacity_model_checksum);
  writer->Key("budget_checksum");
  writer->Integer(value.budget_checksum);
  writer->Key("workload_net_count");
  writer->Integer(value.workload_net_count);
  writer->Key("requested_pool_size");
  writer->Integer(value.requested_pool_size);
  writer->Key("repetition_index");
  writer->Integer(value.repetition_index);
  writer->Key("root_seed");
  writer->Integer(value.root_seed);
  writer->Key("preparation_worker_count");
  writer->Integer(value.preparation_worker_count);
  writer->Key("baseline_sweeps");
  writer->Integer(value.baseline_sweeps);
  writer->Key("candidate_regeneration_epochs");
  writer->Integer(value.candidate_regeneration_epochs);
  writer->Key("candidate_columns_per_epoch");
  writer->Integer(value.candidate_columns_per_epoch);
  writer->Key("candidate_terminal_selection_rounds");
  writer->Integer(value.candidate_terminal_selection_rounds);
  writer->Key("external_budget");
  WriteExternalBudget(writer, value.external_budget);
  writer->Key("opportunity");
  WriteRouteOpportunity(writer, value.opportunity);
  writer->Key("actual");
  WriteRouteOpportunity(writer, value.actual);
  writer->Key("preparation_route_queries");
  writer->Integer(value.preparation_route_queries);
  writer->Key("preparation_route_work_units");
  writer->Integer(value.preparation_route_work_units);
  writer->Key("regeneration_route_queries");
  writer->Integer(value.regeneration_route_queries);
  writer->Key("regeneration_route_work_units");
  writer->Integer(value.regeneration_route_work_units);
  writer->Key("requested_columns");
  writer->Integer(value.requested_columns);
  writer->Key("admitted_candidates");
  writer->Integer(value.admitted_candidates);
  writer->Key("rejected_columns");
  writer->Integer(value.rejected_columns);
  writer->Key("final_candidate_count");
  writer->Integer(value.final_candidate_count);
  writer->Key("preparation_checksum");
  writer->Integer(value.preparation_checksum);
  writer->Key("algorithm_session_checksum");
  writer->Integer(value.algorithm_session_checksum);
  writer->Key("final_pool_manifest_checksum");
  writer->Integer(value.final_pool_manifest_checksum);
  writer->Key("final_rejection_manifest_checksum");
  writer->Integer(value.final_rejection_manifest_checksum);
  writer->Key("terminal_reason");
  writer->EnumValue(value.terminal_reason);
  writer->Key("candidate_outcome_source");
  writer->EnumValue(value.candidate_outcome_source);
  writer->Key("outcome");
  WriteBoardOutcome(writer, value.outcome);
  writer->Key("semantic_checksum");
  writer->Integer(value.semantic_checksum);
  writer->EndObject();
}

void WriteMetrics(JsonWriter* writer, const candidates::CandidateMetrics& value) {
  writer->BeginObject();
  writer->Key("scalar_policy_cost");
  writer->Integer(value.scalar_policy_cost);
  writer->Key("intrinsic_base_cost");
  writer->Integer(value.intrinsic_base_cost);
  writer->Key("orthogonal_step_count");
  writer->Integer(value.orthogonal_step_count);
  writer->Key("diagonal_step_count");
  writer->Integer(value.diagonal_step_count);
  writer->Key("bend_count");
  writer->Integer(value.bend_count);
  writer->Key("line_primitive_count");
  writer->Integer(value.line_primitive_count);
  writer->Key("via_count");
  writer->Integer(value.via_count);
  writer->Key("axis_aligned_length_dbu");
  writer->Integer(value.axis_aligned_length_dbu);
  writer->Key("diagonal_projection_dbu");
  writer->Integer(value.diagonal_projection_dbu);
  writer->EndObject();
}

void WritePerNet(JsonWriter* writer, const Phase4PerNetReportV1& value) {
  writer->BeginObject();
  writer->Key("schema_version");
  writer->Integer(value.schema_version);
  writer->Key("net");
  writer->BeginObject();
  writer->Key("id");
  writer->Integer(value.net.id);
  writer->Key("generation");
  writer->Integer(value.net.generation);
  writer->EndObject();
  writer->Key("columns");
  writer->BeginObject();
  writer->Key("requested_columns");
  writer->Integer(value.columns.requested_columns);
  writer->Key("executed_route_queries");
  writer->Integer(value.columns.executed_route_queries);
  writer->Key("admitted_candidates");
  writer->Integer(value.columns.admitted_candidates);
  writer->Key("duplicate_candidates");
  writer->Integer(value.columns.duplicate_candidates);
  writer->Key("disconnected_columns");
  writer->Integer(value.columns.disconnected_columns);
  writer->Key("unsupported_columns");
  writer->Integer(value.columns.unsupported_columns);
  writer->Key("skipped_columns");
  writer->Integer(value.columns.skipped_columns);
  writer->Key("exact_validation_rejections");
  writer->Integer(value.columns.exact_validation_rejections);
  writer->Key("other_rejections");
  writer->Integer(value.columns.other_rejections);
  writer->EndObject();
  writer->Key("final_pool_size");
  writer->Integer(value.final_pool_size);
  writer->Key("unique_geometry_signature_count");
  writer->Integer(value.unique_geometry_signature_count);
  writer->Key("unique_resource_signature_count");
  writer->Integer(value.unique_resource_signature_count);
  writer->Key("candidate_pair_count");
  writer->Integer(value.candidate_pair_count);
  writer->Key("mean_resource_overlap_ppm");
  writer->Integer(value.mean_resource_overlap_ppm);
  writer->Key("minimum_resource_overlap_ppm");
  writer->Integer(value.minimum_resource_overlap_ppm);
  writer->Key("mean_geometric_overlap_ppm");
  writer->Integer(value.mean_geometric_overlap_ppm);
  writer->Key("minimum_geometric_overlap_ppm");
  writer->Integer(value.minimum_geometric_overlap_ppm);
  writer->Key("selected_status");
  writer->EnumValue(value.selected_status);
  writer->Key("selected_candidate_id");
  if (value.selected_candidate_id.has_value()) {
    writer->BeginObject();
    writer->Key("high");
    writer->Integer(value.selected_candidate_id->high);
    writer->Key("low");
    writer->Integer(value.selected_candidate_id->low);
    writer->EndObject();
  } else {
    writer->Null();
  }
  writer->Key("selected_candidate_payload_checksum");
  if (value.selected_candidate_payload_checksum.has_value()) {
    writer->Integer(*value.selected_candidate_payload_checksum);
  } else {
    writer->Null();
  }
  writer->Key("selected_candidate_metrics");
  if (value.selected_candidate_metrics.has_value()) {
    WriteMetrics(writer, *value.selected_candidate_metrics);
  } else {
    writer->Null();
  }
  writer->Key("pool_best_intrinsic_cost");
  if (value.pool_best_intrinsic_cost.has_value()) {
    writer->Integer(*value.pool_best_intrinsic_cost);
  } else {
    writer->Null();
  }
  writer->EndObject();
}

void WriteTelemetry(JsonWriter* writer, const Phase4ArmReportTelemetryV1& value) {
  writer->BeginObject();
  writer->Key("schema_version");
  writer->Integer(value.schema_version);
  writer->Key("associated_semantic_checksum");
  writer->Integer(value.associated_semantic_checksum);
  writer->Key("per_net");
  writer->BeginArray();
  for (const Phase4PerNetReportV1& report : value.per_net) {
    writer->Element();
    WritePerNet(writer, report);
  }
  writer->EndArray();
  writer->Key("telemetry_checksum");
  writer->Integer(value.telemetry_checksum);
  writer->EndObject();
}

[[nodiscard]] std::string CanonicalRosterManifestPayloadV2() {
  JsonWriter writer;
  writer.BeginObject();
  writer.Key("schema_version");
  writer.Integer(kPhase4WorkloadNetRosterManifestSchemaVersionV2);
  writer.Key("corpus_version");
  writer.Integer(kPhase4RepresentativeCorpusVersionV2);
  writer.Key("corpus_checksum");
  writer.Integer(Phase4RepresentativeCorpusChecksumV2());
  writer.Key("representative_manifest_checksum");
  writer.Integer(kRepresentativeManifestChecksumV2);
  writer.Key("successful_cases");
  writer.BeginArray();
  for (const Phase4WorkloadNetRosterManifestEntryV1& entry : kRosterManifestV2) {
    writer.Element();
    writer.BeginObject();
    writer.Key("schema_version");
    writer.Integer(entry.schema_version);
    writer.Key("corpus_version");
    writer.Integer(entry.corpus_version);
    writer.Key("corpus_checksum");
    writer.Integer(entry.corpus_checksum);
    writer.Key("case_id");
    writer.Integer(entry.case_id);
    writer.Key("descriptor_fingerprint");
    writer.Integer(entry.descriptor_fingerprint);
    writer.Key("case_checksum");
    writer.Integer(entry.case_checksum);
    writer.Key("board_content_hash");
    writer.Integer(entry.board_content_hash);
    writer.Key("workload_checksum");
    writer.Integer(entry.workload_checksum);
    writer.Key("workload_net_count");
    writer.Integer(entry.workload_net_count);
    writer.Key("roster_checksum");
    writer.Integer(entry.roster_checksum);
    writer.EndObject();
  }
  writer.EndArray();
  writer.Key("excluded_cases");
  writer.BeginArray();
  for (const Phase4WorkloadNetRosterManifestExclusionV1& exclusion : kRosterExclusionsV2) {
    writer.Element();
    writer.BeginObject();
    writer.Key("case_id");
    writer.Integer(exclusion.case_id);
    writer.Key("descriptor_fingerprint");
    writer.Integer(exclusion.descriptor_fingerprint);
    writer.Key("disposition");
    writer.String(
        exclusion.disposition ==
                Phase4WorkloadNetRosterExclusionDispositionV1::kDescriptorOnlyUnsupportedPool
            ? "descriptor_only_unsupported_pool"
            : "compiled_work_bound");
    writer.EndObject();
  }
  writer.EndArray();
  writer.EndObject();
  std::string payload = std::move(writer).Finish();
  payload.pop_back();
  return payload;
}

}  // namespace

std::span<const Phase4WorkloadNetRosterManifestEntryV1>
Phase4WorkloadNetRosterManifestV1() noexcept {
  return kRosterManifestV1;
}

std::span<const Phase4WorkloadNetRosterManifestEntryV1>
Phase4WorkloadNetRosterManifestV2() noexcept {
  return kRosterManifestV2;
}

std::span<const Phase4WorkloadNetRosterManifestExclusionV1>
Phase4WorkloadNetRosterManifestExclusionsV1() noexcept {
  return kRosterExclusionsV1;
}

std::span<const Phase4WorkloadNetRosterManifestExclusionV1>
Phase4WorkloadNetRosterManifestExclusionsV2() noexcept {
  return kRosterExclusionsV2;
}

std::uint64_t ComputePhase4WorkloadNetRosterManifestChecksumV1() noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-PHASE4-WORKLOAD-NET-ROSTER-MANIFEST-V1");
  hash.AddU32(kPhase4WorkloadNetRosterManifestSchemaVersion);
  hash.AddU32(kPhase4RepresentativeCorpusVersion);
  hash.AddU64(Phase4RepresentativeCorpusChecksumV1());
  hash.AddU64(kRosterManifestV1.size());
  for (const Phase4WorkloadNetRosterManifestEntryV1& entry : kRosterManifestV1) {
    hash.AddU32(entry.schema_version);
    hash.AddU32(entry.corpus_version);
    hash.AddU64(entry.corpus_checksum);
    hash.AddU32(entry.case_id);
    hash.AddU64(entry.descriptor_fingerprint);
    hash.AddU64(entry.case_checksum);
    hash.AddU64(entry.board_content_hash);
    hash.AddU64(entry.workload_checksum);
    hash.AddU32(entry.workload_net_count);
    hash.AddU64(entry.roster_checksum);
  }
  hash.AddU64(kRosterExclusionsV1.size());
  for (const Phase4WorkloadNetRosterManifestExclusionV1& exclusion : kRosterExclusionsV1) {
    hash.AddU32(exclusion.case_id);
    hash.AddU64(exclusion.descriptor_fingerprint);
    hash.AddByte(static_cast<std::uint8_t>(exclusion.disposition));
  }
  return hash.Finish();
}

std::uint64_t ComputePhase4WorkloadNetRosterManifestChecksumV2() {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-PHASE4-WORKLOAD-NET-ROSTER-MANIFEST-V2");
  hash.AddString(CanonicalRosterManifestPayloadV2());
  return hash.Finish();
}

const Phase4WorkloadNetRosterManifestEntryV1* FindPhase4WorkloadNetRosterManifestEntryV1(
    std::uint32_t case_id) noexcept {
  const auto found = std::ranges::lower_bound(kRosterManifestV1, case_id, {},
                                              &Phase4WorkloadNetRosterManifestEntryV1::case_id);
  return found != kRosterManifestV1.end() && found->case_id == case_id ? &*found : nullptr;
}

const Phase4WorkloadNetRosterManifestEntryV1* FindPhase4WorkloadNetRosterManifestEntryV2(
    std::uint32_t case_id) noexcept {
  const auto found = std::ranges::lower_bound(kRosterManifestV2, case_id, {},
                                              &Phase4WorkloadNetRosterManifestEntryV1::case_id);
  return found != kRosterManifestV2.end() && found->case_id == case_id ? &*found : nullptr;
}

std::uint64_t ComputePhase4WorkloadNetRosterChecksumV1(
    const Phase4RepresentativeCase& representative_case) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-PHASE4-WORKLOAD-NET-ROSTER-V1");
  hash.AddU32(kPhase4WorkloadNetRosterManifestSchemaVersion);
  hash.AddU32(kPhase4RepresentativeCorpusVersion);
  hash.AddU64(Phase4RepresentativeCorpusChecksumV1());
  hash.AddU32(representative_case.descriptor.case_id);
  hash.AddU64(FingerprintPhase4CaseDescriptorV1(representative_case.descriptor));
  hash.AddU64(representative_case.case_checksum);
  hash.AddU64(representative_case.board.content_hash());
  hash.AddU64(representative_case.workload.workload_checksum());
  hash.AddU64(representative_case.workload.nets().size());
  for (const allocator::PreparedNetRoutingContext& context : representative_case.workload.nets()) {
    hash.AddU64(context.request.net.id);
    hash.AddU32(context.request.net.generation);
  }
  return hash.Finish();
}

std::uint64_t ComputePhase4WorkloadNetRosterChecksumV2(
    const Phase4RepresentativeCase& representative_case) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-PHASE4-WORKLOAD-NET-ROSTER-V2");
  hash.AddU32(kPhase4WorkloadNetRosterManifestSchemaVersionV2);
  hash.AddU32(kPhase4RepresentativeCorpusVersionV2);
  hash.AddU64(Phase4RepresentativeCorpusChecksumV2());
  hash.AddU32(representative_case.descriptor.case_id);
  hash.AddU64(FingerprintPhase4CaseDescriptorV2(representative_case.descriptor));
  hash.AddU64(representative_case.case_checksum);
  hash.AddU64(representative_case.board.content_hash());
  hash.AddU64(representative_case.workload.workload_checksum());
  hash.AddU64(representative_case.workload.nets().size());
  for (const allocator::PreparedNetRoutingContext& context : representative_case.workload.nets()) {
    hash.AddU64(context.request.net.id);
    hash.AddU32(context.request.net.generation);
  }
  return hash.Finish();
}

std::uint64_t ComputePhase4CanonicalCellPlanChecksumV1(
    const Phase4CanonicalCellConfig& config) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-PHASE4-CANONICAL-CELL-PLAN-V1");
  hash.AddU32(config.schema_version);
  hash.AddU64(Phase4RepresentativeCorpusChecksumV1());
  hash.AddU32(config.case_id);
  hash.AddU32(config.requested_pool_size);
  hash.AddU32(config.preparation_worker_count);
  hash.AddU32(config.repetitions);
  hash.AddU64(config.maximum_setup_elapsed_nanoseconds);
  HashExternalBudget(&hash, config.external_budget);
  HashCorpusLimits(&hash, config.corpus_limits);
  return hash.Finish();
}

std::uint64_t ComputePhase4CanonicalCellPlanChecksumForCorpusV2(
    const Phase4CanonicalCellConfig& config) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-PHASE4-CANONICAL-CELL-PLAN-V2");
  hash.AddU32(config.schema_version);
  hash.AddU64(Phase4RepresentativeCorpusChecksumV2());
  hash.AddU32(config.case_id);
  hash.AddU32(config.requested_pool_size);
  hash.AddU32(config.preparation_worker_count);
  hash.AddU32(config.repetitions);
  hash.AddU64(config.maximum_setup_elapsed_nanoseconds);
  HashExternalBudget(&hash, config.external_budget);
  HashCorpusLimits(&hash, config.corpus_limits);
  return hash.Finish();
}

std::uint64_t ComputePhase4PerNetReportArtifactChecksumV1(
    const Phase4PerNetReportArtifactV1& artifact) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-PHASE4-PER-NET-REPORT-ARTIFACT-V1");
  hash.AddU32(artifact.schema_version);
  hash.AddU32(artifact.raw_wire_schema_version);
  HashCellConfig(&hash, artifact.config);
  hash.AddU64(artifact.corpus_checksum);
  hash.AddU64(artifact.raw_cell_plan_checksum);
  hash.AddU64(artifact.raw_cell_artifact_checksum);
  hash.AddU64(artifact.raw_source_envelope_checksum);
  hash.AddU32(artifact.raw_reference.repetition_index);
  hash.AddByte(static_cast<std::uint8_t>(artifact.raw_reference.execution_order));
  hash.AddU64(artifact.raw_reference.pair_attempt_checksum);
  hash.AddU64(artifact.raw_reference.paired_semantic_checksum);
  hash.AddU64(artifact.raw_reference.paired_artifact_checksum);
  hash.AddU64(artifact.raw_reference.baseline_semantic_checksum);
  hash.AddU64(artifact.raw_reference.baseline_arm_artifact_checksum);
  hash.AddU64(artifact.raw_reference.candidate_semantic_checksum);
  hash.AddU64(artifact.raw_reference.candidate_arm_artifact_checksum);
  hash.AddBool(artifact.decision_eligible);
  hash.AddU64(artifact.workload_net_roster_checksum);
  hash.AddU64(artifact.arms.size());
  for (const Phase4PerNetReportArmArtifactV1& arm : artifact.arms) {
    hash.AddByte(static_cast<std::uint8_t>(arm.arm));
    hash.AddU64(arm.raw_semantic_checksum);
    hash.AddU64(arm.diagnostic.semantics.semantic_checksum);
    hash.AddU64(arm.diagnostic.telemetry.telemetry_checksum);
  }
  return hash.Finish();
}

std::uint64_t ComputePhase4PerNetReportSourceEnvelopeChecksumV1(
    std::string_view source_commit, bool source_stamped, bool source_tree_dirty,
    std::uint64_t artifact_checksum) noexcept {
  board_ir::StableHashBuilder hash;
  hash.AddString("APGAR-PHASE4-PER-NET-REPORT-SOURCE-ENVELOPE-V1");
  hash.AddString(source_commit);
  hash.AddBool(source_stamped);
  hash.AddBool(source_tree_dirty);
  hash.AddU64(artifact_checksum);
  return hash.Finish();
}

std::variant<std::monostate, Phase4PerNetReportArtifactError>
ValidatePhase4PerNetReportArtifactForAuthority(const Phase4PerNetReportArtifactV1& artifact,
                                               std::string_view imported_fixture, bool corpus_v2) {
  try {
    if (artifact.schema_version != kPhase4PerNetReportArtifactSchemaVersion ||
        (artifact.raw_wire_schema_version != kPhase4TrialWireSchemaVersion &&
         artifact.raw_wire_schema_version != kPhase4SameRunTrialWireSchemaVersion) ||
        !IsLowerHexCommit(artifact.source_commit) || !artifact.source_stamped ||
        artifact.source_tree_dirty || artifact.decision_eligible) {
      return Error("P4REPORT-ARTIFACT-ENVELOPE-001",
                   "report schema, clean stamped source, or diagnostic eligibility is invalid");
    }
    const std::uint64_t roster_manifest_checksum =
        corpus_v2 ? ComputePhase4WorkloadNetRosterManifestChecksumV2()
                  : ComputePhase4WorkloadNetRosterManifestChecksumV1();
    const std::uint64_t expected_roster_manifest_checksum =
        corpus_v2 ? kPhase4WorkloadNetRosterManifestChecksumV2
                  : kPhase4WorkloadNetRosterManifestChecksumV1;
    if (roster_manifest_checksum != expected_roster_manifest_checksum) {
      return Error("P4REPORT-ROSTER-MANIFEST-CHECKSUM-001",
                   "compiled workload-net roster manifest constants have drifted");
    }
    if (artifact.artifact_checksum == 0 ||
        artifact.artifact_checksum != ComputePhase4PerNetReportArtifactChecksumV1(artifact) ||
        artifact.source_envelope_checksum == 0 ||
        artifact.source_envelope_checksum != ComputePhase4PerNetReportSourceEnvelopeChecksumV1(
                                                 artifact.source_commit, artifact.source_stamped,
                                                 artifact.source_tree_dirty,
                                                 artifact.artifact_checksum)) {
      return Error("P4REPORT-ARTIFACT-CHECKSUM-001",
                   "report artifact or report source-envelope checksum is invalid");
    }
    if (artifact.raw_cell_plan_checksum == 0 || artifact.raw_cell_artifact_checksum == 0 ||
        artifact.raw_source_envelope_checksum == 0 ||
        artifact.raw_cell_plan_checksum !=
            (corpus_v2 ? ComputePhase4CanonicalCellPlanChecksumForCorpusV2(artifact.config)
                       : ComputePhase4CanonicalCellPlanChecksumV1(artifact.config)) ||
        artifact.raw_source_envelope_checksum !=
            (artifact.raw_wire_schema_version == kPhase4SameRunTrialWireSchemaVersion
                 ? ComputePhase4SourceEnvelopeChecksumV2(
                       kPhase4SameRunRawEvidenceSchemaVersion, artifact.raw_wire_schema_version,
                       artifact.source_commit, artifact.source_stamped, artifact.source_tree_dirty,
                       artifact.raw_cell_artifact_checksum)
                 : ComputePhase4SourceEnvelopeChecksumV1(
                       artifact.raw_wire_schema_version, artifact.source_commit,
                       artifact.source_stamped, artifact.source_tree_dirty,
                       artifact.raw_cell_artifact_checksum))) {
      return Error("P4REPORT-RAW-REFERENCE-001",
                   "raw wire, cell-plan, artifact, or source-envelope association is invalid");
    }
    const Phase4PerNetReportRawReferenceV1& raw = artifact.raw_reference;
    if (raw.repetition_index != 0 || raw.execution_order != Phase4TrialOrder::kBaselineFirst ||
        raw.pair_attempt_checksum == 0 || raw.paired_semantic_checksum == 0 ||
        raw.paired_artifact_checksum == 0 || raw.baseline_semantic_checksum == 0 ||
        raw.baseline_arm_artifact_checksum == 0 || raw.candidate_semantic_checksum == 0 ||
        raw.candidate_arm_artifact_checksum == 0) {
      return Error("P4REPORT-RAW-REFERENCE-002",
                   "report must reference one complete repetition-zero baseline-first raw pair");
    }
    if (artifact.config.preparation_worker_count != kPhase4CanonicalPreparationWorkersV1 ||
        artifact.config.repetitions != kPhase4CanonicalRepetitionsV1) {
      return Error("P4REPORT-CONFIG-001",
                   "report must bind the canonical worker and repetition configuration");
    }
    Phase4CanonicalSpecResult spec_result =
        corpus_v2
            ? BuildPhase4CanonicalTrialSpecForCorpusV2(artifact.config, 0,
                                                       Phase4TrialOrder::kBaselineFirst)
            : BuildPhase4CanonicalTrialSpecV1(artifact.config, 0, Phase4TrialOrder::kBaselineFirst);
    if (!std::holds_alternative<Phase4PairedTrialSpec>(spec_result)) {
      return Error("P4REPORT-CONFIG-002", "report cell configuration is not canonical");
    }
    const Phase4PairedTrialSpec spec = std::get<Phase4PairedTrialSpec>(std::move(spec_result));
    const std::uint64_t expected_corpus_checksum =
        corpus_v2 ? Phase4RepresentativeCorpusChecksumV2() : Phase4RepresentativeCorpusChecksumV1();
    if (artifact.corpus_checksum != expected_corpus_checksum) {
      return Error("P4REPORT-CORPUS-001", "report names a foreign representative corpus");
    }
    Phase4RepresentativeCaseResult case_result =
        corpus_v2 ? BuildPhase4RepresentativeCaseV2(artifact.config.case_id, imported_fixture,
                                                    artifact.config.corpus_limits)
                  : BuildPhase4RepresentativeCaseV1(artifact.config.case_id, imported_fixture,
                                                    artifact.config.corpus_limits);
    if (!std::holds_alternative<Phase4RepresentativeCase>(case_result)) {
      return Error("P4REPORT-CASE-001", "the referenced representative case cannot be rebuilt");
    }
    const Phase4RepresentativeCase representative_case =
        std::get<Phase4RepresentativeCase>(std::move(case_result));
    const Phase4WorkloadNetRosterManifestEntryV1* frozen =
        corpus_v2 ? FindPhase4WorkloadNetRosterManifestEntryV2(artifact.config.case_id)
                  : FindPhase4WorkloadNetRosterManifestEntryV1(artifact.config.case_id);
    const std::uint32_t roster_schema_version =
        corpus_v2 ? kPhase4WorkloadNetRosterManifestSchemaVersionV2
                  : kPhase4WorkloadNetRosterManifestSchemaVersion;
    const std::uint32_t corpus_version =
        corpus_v2 ? kPhase4RepresentativeCorpusVersionV2 : kPhase4RepresentativeCorpusVersion;
    const std::uint64_t descriptor_fingerprint =
        corpus_v2 ? FingerprintPhase4CaseDescriptorV2(representative_case.descriptor)
                  : FingerprintPhase4CaseDescriptorV1(representative_case.descriptor);
    if (frozen == nullptr || frozen->schema_version != roster_schema_version ||
        frozen->corpus_version != corpus_version ||
        frozen->corpus_checksum != artifact.corpus_checksum ||
        frozen->descriptor_fingerprint != descriptor_fingerprint ||
        frozen->case_checksum != representative_case.case_checksum ||
        frozen->board_content_hash != representative_case.board.content_hash() ||
        frozen->workload_checksum != representative_case.workload.workload_checksum() ||
        frozen->workload_net_count != representative_case.workload.nets().size()) {
      return Error("P4REPORT-ROSTER-MANIFEST-001",
                   "rebuilt case identity does not match the frozen successful-case roster row");
    }
    const std::uint64_t roster_checksum =
        corpus_v2 ? ComputePhase4WorkloadNetRosterChecksumV2(representative_case)
                  : ComputePhase4WorkloadNetRosterChecksumV1(representative_case);
    if (roster_checksum == 0 || roster_checksum != frozen->roster_checksum ||
        artifact.workload_net_roster_checksum != roster_checksum) {
      return Error("P4REPORT-ROSTER-001",
                   "the complete rebuilt workload EntityRef roster is not independently frozen");
    }

    const Phase4RepresentativeCorpusAuthority authority =
        corpus_v2 ? Phase4RepresentativeCorpusAuthority::kV2
                  : Phase4RepresentativeCorpusAuthority::kV1;
    const std::uint64_t budget_checksum = internal::ComputePhase4PairedBudgetChecksumForAuthorityV1(
        authority, spec,
        Phase4RouteOpportunity{
            .route_queries = spec.baseline_config.limits.maximum_route_queries,
            .route_work_units = spec.baseline_config.limits.maximum_total_route_work_units,
        },
        frozen->workload_net_count,
        spec.candidate_session_config.regeneration_plan_config.maximum_total_columns,
        spec.candidate_session_config.schedules.back().maximum_selection_rounds);
    const std::array<Phase4TrialArm, 2> expected_arms = {
        Phase4TrialArm::kSequentialBaseline,
        Phase4TrialArm::kReusableCandidateAllocation,
    };
    const std::array<std::uint64_t, 2> raw_semantics = {
        raw.baseline_semantic_checksum,
        raw.candidate_semantic_checksum,
    };
    for (std::size_t index = 0; index < artifact.arms.size(); ++index) {
      const Phase4PerNetReportArmArtifactV1& arm = artifact.arms[index];
      if (arm.arm != expected_arms[index] || arm.diagnostic.semantics.arm != arm.arm) {
        return Error("P4REPORT-ARM-IDENTITY-001",
                     "diagnostic arms are not ordered baseline then candidate");
      }
      if (arm.raw_semantic_checksum != raw_semantics[index] ||
          arm.diagnostic.semantics.semantic_checksum != arm.raw_semantic_checksum) {
        return Error("P4REPORT-ARM-IDENTITY-002",
                     "diagnostic arm semantic checksum differs from its raw reference");
      }
      if (!SameSemanticConfig(arm.diagnostic.semantics, arm.arm, spec, representative_case,
                              budget_checksum, corpus_v2)) {
        return Error("P4REPORT-ARM-IDENTITY-001",
                     "diagnostic arm semantics differ from canonical case and budget identity");
      }
      if (std::optional<Phase4PairedTrialError> telemetry_error =
              internal::ValidatePhase4ArmReportTelemetryForAuthorityV1(
                  authority, arm.diagnostic.semantics, representative_case.workload,
                  arm.diagnostic.telemetry);
          telemetry_error.has_value()) {
        return Error("P4REPORT-ARM-TELEMETRY-001",
                     "diagnostic per-net telemetry failed authentic workload validation");
      }
    }
    return std::monostate{};
  } catch (const std::bad_alloc&) {
    return Error("P4REPORT-HOST-001", "host allocation failed while validating report artifact");
  } catch (const std::length_error&) {
    return Error("P4REPORT-HOST-002",
                 "host container length failed while validating report artifact");
  } catch (const std::exception&) {
    return Error("P4REPORT-HOST-003",
                 "unexpected standard exception while validating report artifact");
  } catch (...) {
    return Error("P4REPORT-HOST-004",
                 "unexpected non-standard exception while validating report artifact");
  }
}

std::variant<std::monostate, Phase4PerNetReportArtifactError> ValidatePhase4PerNetReportArtifactV1(
    const Phase4PerNetReportArtifactV1& artifact, std::string_view imported_fixture) {
  return ValidatePhase4PerNetReportArtifactForAuthority(artifact, imported_fixture, false);
}

std::variant<std::monostate, Phase4PerNetReportArtifactError>
ValidatePhase4PerNetReportArtifactForCorpusV2(const Phase4PerNetReportArtifactV1& artifact,
                                              std::string_view imported_fixture) {
  return ValidatePhase4PerNetReportArtifactForAuthority(artifact, imported_fixture, true);
}

Phase4PerNetReportArtifactResultV1 BuildPhase4PerNetReportArtifactForAuthority(
    const Phase4CanonicalCellConfig& config, std::string_view source_commit, bool source_stamped,
    bool source_tree_dirty, std::uint64_t raw_cell_plan_checksum,
    std::uint64_t raw_cell_artifact_checksum, std::uint64_t raw_source_envelope_checksum,
    Phase4PerNetReportRawReferenceV1 raw_reference,
    std::array<Phase4TrialArmDiagnosticExecutionV1, 2> diagnostics,
    std::string_view imported_fixture, std::uint32_t raw_wire_schema_version, bool corpus_v2) {
  if (!IsLowerHexCommit(source_commit)) {
    return Error("P4REPORT-BUILD-SOURCE-001",
                 "source commit must be exactly 40 lowercase hexadecimal characters");
  }
  try {
    Phase4PerNetReportArtifactV1 artifact;
    artifact.source_commit = std::string(source_commit);
    artifact.source_stamped = source_stamped;
    artifact.source_tree_dirty = source_tree_dirty;
    artifact.raw_wire_schema_version = raw_wire_schema_version;
    artifact.config = config;
    artifact.corpus_checksum =
        corpus_v2 ? Phase4RepresentativeCorpusChecksumV2() : Phase4RepresentativeCorpusChecksumV1();
    artifact.raw_cell_plan_checksum = raw_cell_plan_checksum;
    artifact.raw_cell_artifact_checksum = raw_cell_artifact_checksum;
    artifact.raw_source_envelope_checksum = raw_source_envelope_checksum;
    artifact.raw_reference = raw_reference;
    const Phase4WorkloadNetRosterManifestEntryV1* frozen =
        corpus_v2 ? FindPhase4WorkloadNetRosterManifestEntryV2(config.case_id)
                  : FindPhase4WorkloadNetRosterManifestEntryV1(config.case_id);
    artifact.workload_net_roster_checksum = frozen == nullptr ? 0 : frozen->roster_checksum;
    artifact.arms = {
        Phase4PerNetReportArmArtifactV1{
            .arm = diagnostics[0].semantics.arm,
            .raw_semantic_checksum = raw_reference.baseline_semantic_checksum,
            .diagnostic = std::move(diagnostics[0]),
        },
        Phase4PerNetReportArmArtifactV1{
            .arm = diagnostics[1].semantics.arm,
            .raw_semantic_checksum = raw_reference.candidate_semantic_checksum,
            .diagnostic = std::move(diagnostics[1]),
        },
    };
    artifact.artifact_checksum = ComputePhase4PerNetReportArtifactChecksumV1(artifact);
    artifact.source_envelope_checksum = ComputePhase4PerNetReportSourceEnvelopeChecksumV1(
        artifact.source_commit, artifact.source_stamped, artifact.source_tree_dirty,
        artifact.artifact_checksum);
    auto validation =
        ValidatePhase4PerNetReportArtifactForAuthority(artifact, imported_fixture, corpus_v2);
    if (std::holds_alternative<Phase4PerNetReportArtifactError>(validation)) {
      return std::get<Phase4PerNetReportArtifactError>(std::move(validation));
    }
    return artifact;
  } catch (const std::bad_alloc&) {
    return Error("P4REPORT-BUILD-HOST-001",
                 "host allocation failed while building report artifact");
  } catch (const std::length_error&) {
    return Error("P4REPORT-BUILD-HOST-002",
                 "host container length failed while building report artifact");
  } catch (const std::exception&) {
    return Error("P4REPORT-BUILD-HOST-003",
                 "unexpected standard exception while building report artifact");
  } catch (...) {
    return Error("P4REPORT-BUILD-HOST-004",
                 "unexpected non-standard exception while building report artifact");
  }
}

Phase4PerNetReportArtifactResultV1 BuildPhase4PerNetReportArtifactV1(
    const Phase4CanonicalCellConfig& config, std::string_view source_commit, bool source_stamped,
    bool source_tree_dirty, std::uint64_t raw_cell_plan_checksum,
    std::uint64_t raw_cell_artifact_checksum, std::uint64_t raw_source_envelope_checksum,
    Phase4PerNetReportRawReferenceV1 raw_reference,
    std::array<Phase4TrialArmDiagnosticExecutionV1, 2> diagnostics,
    std::string_view imported_fixture, std::uint32_t raw_wire_schema_version) {
  return BuildPhase4PerNetReportArtifactForAuthority(
      config, source_commit, source_stamped, source_tree_dirty, raw_cell_plan_checksum,
      raw_cell_artifact_checksum, raw_source_envelope_checksum, raw_reference,
      std::move(diagnostics), imported_fixture, raw_wire_schema_version, false);
}

Phase4PerNetReportArtifactResultV1 BuildPhase4PerNetReportArtifactForCorpusV2(
    const Phase4CanonicalCellConfig& config, std::string_view source_commit, bool source_stamped,
    bool source_tree_dirty, std::uint64_t raw_cell_plan_checksum,
    std::uint64_t raw_cell_artifact_checksum, std::uint64_t raw_source_envelope_checksum,
    Phase4PerNetReportRawReferenceV1 raw_reference,
    std::array<Phase4TrialArmDiagnosticExecutionV1, 2> diagnostics,
    std::string_view imported_fixture, std::uint32_t raw_wire_schema_version) {
  return BuildPhase4PerNetReportArtifactForAuthority(
      config, source_commit, source_stamped, source_tree_dirty, raw_cell_plan_checksum,
      raw_cell_artifact_checksum, raw_source_envelope_checksum, raw_reference,
      std::move(diagnostics), imported_fixture, raw_wire_schema_version, true);
}

std::string SerializePhase4PerNetReportArtifactJsonV1(
    const Phase4PerNetReportArtifactV1& artifact) {
  JsonWriter writer;
  writer.BeginObject();
  writer.Key("source_commit");
  writer.String(artifact.source_commit);
  writer.Key("source_stamped");
  writer.Bool(artifact.source_stamped);
  writer.Key("source_tree_dirty");
  writer.Bool(artifact.source_tree_dirty);
  writer.Key("source_envelope_checksum");
  writer.Integer(artifact.source_envelope_checksum);
  writer.Key("schema_version");
  writer.Integer(artifact.schema_version);
  writer.Key("raw_wire_schema_version");
  writer.Integer(artifact.raw_wire_schema_version);
  writer.Key("config");
  WriteCellConfig(&writer, artifact.config);
  writer.Key("corpus_checksum");
  writer.Integer(artifact.corpus_checksum);
  writer.Key("raw_cell_plan_checksum");
  writer.Integer(artifact.raw_cell_plan_checksum);
  writer.Key("raw_cell_artifact_checksum");
  writer.Integer(artifact.raw_cell_artifact_checksum);
  writer.Key("raw_source_envelope_checksum");
  writer.Integer(artifact.raw_source_envelope_checksum);
  writer.Key("raw_reference");
  writer.BeginObject();
  writer.Key("repetition_index");
  writer.Integer(artifact.raw_reference.repetition_index);
  writer.Key("execution_order");
  writer.EnumValue(artifact.raw_reference.execution_order);
  writer.Key("pair_attempt_checksum");
  writer.Integer(artifact.raw_reference.pair_attempt_checksum);
  writer.Key("paired_semantic_checksum");
  writer.Integer(artifact.raw_reference.paired_semantic_checksum);
  writer.Key("paired_artifact_checksum");
  writer.Integer(artifact.raw_reference.paired_artifact_checksum);
  writer.Key("baseline_semantic_checksum");
  writer.Integer(artifact.raw_reference.baseline_semantic_checksum);
  writer.Key("baseline_arm_artifact_checksum");
  writer.Integer(artifact.raw_reference.baseline_arm_artifact_checksum);
  writer.Key("candidate_semantic_checksum");
  writer.Integer(artifact.raw_reference.candidate_semantic_checksum);
  writer.Key("candidate_arm_artifact_checksum");
  writer.Integer(artifact.raw_reference.candidate_arm_artifact_checksum);
  writer.EndObject();
  writer.Key("decision_eligible");
  writer.Bool(artifact.decision_eligible);
  writer.Key("workload_net_roster_checksum");
  writer.Integer(artifact.workload_net_roster_checksum);
  writer.Key("arms");
  writer.BeginArray();
  for (const Phase4PerNetReportArmArtifactV1& arm : artifact.arms) {
    writer.Element();
    writer.BeginObject();
    writer.Key("arm");
    writer.EnumValue(arm.arm);
    writer.Key("raw_semantic_checksum");
    writer.Integer(arm.raw_semantic_checksum);
    writer.Key("diagnostic");
    writer.BeginObject();
    writer.Key("semantics");
    WriteArmSemantics(&writer, arm.diagnostic.semantics);
    writer.Key("telemetry");
    WriteTelemetry(&writer, arm.diagnostic.telemetry);
    writer.EndObject();
    writer.EndObject();
  }
  writer.EndArray();
  writer.Key("artifact_checksum");
  writer.Integer(artifact.artifact_checksum);
  writer.EndObject();
  return std::move(writer).Finish();
}

}  // namespace apgar::benchmark
