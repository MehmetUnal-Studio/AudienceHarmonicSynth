#include "PartialEngine.h"
#include "AtomicScaleBuilder.h"
#include "ElementSpectralData.h"
#include <cmath>
#include <cstring>
#include <limits>

namespace
{
    constexpr float kPi    = 3.14159265358979323846f;
    constexpr float kTwoPi = 6.28318530717958647692f;

    // Shared, read-only sine wavetable for the additive partial oscillator.
    // Built ONCE at static-init time (not on the audio thread, not per voice).
    // Guard entry [kSineTableSize] == [0] enables branch-free linear interpolation.
    // Linear interp on a 4096-point table is ~-78 dB error (inaudible).
    constexpr int kSineTableSize = 4096;
    static const std::array<float, kSineTableSize + 1> kSineTable = []
    {
        std::array<float, kSineTableSize + 1> table {};
        for (int i = 0; i < kSineTableSize; ++i)
            table[(size_t) i] = (float) std::sin(kTwoPi * (double) i / (double) kSineTableSize);
        table[(size_t) kSineTableSize] = table[0];
        return table;
    }();

	    struct ScaleDef
	    {
	        const char* name;
	        std::array<int, 7> degrees;
	        int count;
	    };

	    constexpr std::array<ScaleDef, 7> kScales { {
	        { "Major",          { 0, 2, 4, 5, 7, 9, 11 }, 7 },
	        { "Natural Minor",  { 0, 2, 3, 5, 7, 8, 10 }, 7 },
	        { "Pentatonic",     { 0, 2, 4, 7, 9, 0,  0  }, 5 },
	        { "Dorian",         { 0, 2, 3, 5, 7, 9, 10 }, 7 },
	        { "Lydian",         { 0, 2, 4, 6, 7, 9, 11 }, 7 },
	        { "Harmonic Minor", { 0, 2, 3, 5, 7, 8, 11 }, 7 },
	        { "Whole Tone",     { 0, 2, 4, 6, 8, 10, 0  }, 6 },
	    } };

	    constexpr int kHydrogenMode = (int) kScales.size();
	    constexpr int kHeliumMode = kHydrogenMode + 1;
        constexpr int kLithiumMode = kHeliumMode + 1;
        constexpr int kBerylliumMode = kLithiumMode + 1;
        constexpr int kBoronMode = kBerylliumMode + 1;
        constexpr int kCarbonMode = kBoronMode + 1;
        constexpr int kOxygenMode = kCarbonMode + 1;
        constexpr int kFluorineMode = kOxygenMode + 1;
        constexpr int kNeonMode = kFluorineMode + 1;
        constexpr int kSodiumMode = kNeonMode + 1;
        constexpr int kMagnesiumMode = kSodiumMode + 1;
        constexpr int kAluminiumMode = kMagnesiumMode + 1;
        constexpr int kSiliconMode = kAluminiumMode + 1;
        constexpr int kPhosphorusMode = kSiliconMode + 1;
        constexpr int kSulfurMode = kPhosphorusMode + 1;
        constexpr int kChlorineMode = kSulfurMode + 1;
        constexpr int kArgonMode = kChlorineMode + 1;
        constexpr int kPotassiumMode = kArgonMode + 1;
        constexpr int kCalciumMode = kPotassiumMode + 1;
        constexpr int kScandiumMode = kCalciumMode + 1;
        constexpr int kTitaniumMode = kScandiumMode + 1;
        constexpr int kVanadiumMode = kTitaniumMode + 1;
        constexpr int kChromiumMode = kVanadiumMode + 1;
        constexpr int kManganeseMode = kChromiumMode + 1;
        constexpr int kIronMode = kManganeseMode + 1;
        constexpr int kCobaltMode = kIronMode + 1;
        constexpr int kNickelMode = kCobaltMode + 1;
        constexpr int kCopperMode = kNickelMode + 1;
        constexpr int kZincMode = kCopperMode + 1;
        constexpr int kTotalScaleModes = kZincMode + 1;
        constexpr int kEngineSampleLibrary = 0;
        constexpr int kEngineElementSynth = 1;
        constexpr int kSamplePlaybackDirect = 0;
        constexpr int kSamplePlaybackGranular = 1;
        constexpr int kElementHydrogen = 0;
        constexpr int kElementHelium = 1;
        constexpr int kElementLithium = 2;
        constexpr int kElementBeryllium = 3;
        constexpr int kElementBoron = 4;
        constexpr int kElementCarbon = 5;
        constexpr int kElementOxygen = 6;
        constexpr int kElementFluorine = 7;
        constexpr int kElementNeon = 8;
        constexpr int kElementSodium = 9;
        constexpr int kElementMagnesium = 10;
        constexpr int kElementAluminium = 11;
        constexpr int kElementSilicon = 12;
        constexpr int kElementPhosphorus = 13;
        constexpr int kElementSulfur = 14;
        constexpr int kElementChlorine = 15;
        constexpr int kElementArgon = 16;
        constexpr int kElementPotassium = 17;
        constexpr int kElementCalcium = 18;
        constexpr int kElementScandium = 19;
        constexpr int kElementTitanium = 20;
        constexpr int kElementVanadium = 21;
        constexpr int kElementChromium = 22;
        constexpr int kElementManganese = 23;
        constexpr int kElementIron = 24;
        constexpr int kElementCobalt = 25;
        constexpr int kElementNickel = 26;
        constexpr int kElementCopper = 27;
        constexpr int kElementZinc = 28;
        constexpr int kLastElement = kElementZinc;

        const std::vector<AtomicScaleBuilder::SourceLine>& hydrogenLines()
        {
            using Unit = AtomicScaleBuilder::WavelengthUnit;
            static const std::vector<AtomicScaleBuilder::SourceLine> lines {
                { "H-656.279", "H-alpha", 656.2790, Unit::Nanometer, 6500.0 },
                { "H-486.135", "H-beta",  486.1350, Unit::Nanometer, 1500.0 },
                { "H-434.047", "H-gamma", 434.0472, Unit::Nanometer, 1000.0 },
                { "H-410.173", "H-delta", 410.1734, Unit::Nanometer, 675.0 },
                { "H-397.008", "H-epsilon", 397.0075, Unit::Nanometer, 255.0 },
                { "H-388.906", "H-zeta", 388.9064, Unit::Nanometer, 195.0 },
                { "H-383.540", "H-eta", 383.5397, Unit::Nanometer, 135.0 },
            };
            return lines;
        }

        const std::vector<AtomicScaleBuilder::SourceLine>& heliumLines()
        {
            using Unit = AtomicScaleBuilder::WavelengthUnit;
            static const std::vector<AtomicScaleBuilder::SourceLine> lines {
                { "He-706.519", "He I 706.519", 706.5190, Unit::Nanometer, 1000.0 },
                { "He-667.815", "He I 667.815", 667.8151, Unit::Nanometer, 870.0 },
                { "He-587.562", "He I 587.562", 587.5621, Unit::Nanometer, 2000.0 },
                { "He-501.568", "He I 501.568", 501.56783, Unit::Nanometer, 345.0 },
                { "He-492.193", "He I 492.193", 492.19313, Unit::Nanometer, 240.0 },
                { "He-471.315", "He I 471.315", 471.31457, Unit::Nanometer, 750.0 },
                { "He-443.755", "He I 443.755", 443.7551, Unit::Nanometer, 990.0 },
                { "He-438.793", "He I 438.793", 438.79296, Unit::Nanometer, 225.0 },
                { "He-388.865", "He I 388.865", 388.8648, Unit::Nanometer, 2000.0 },
            };
            return lines;
        }

        const std::vector<AtomicScaleBuilder::SourceLine>& lithiumLines()
        {
            using Unit = AtomicScaleBuilder::WavelengthUnit;
            static const std::vector<AtomicScaleBuilder::SourceLine> lines {
                { "Li-670.791", "Li I 670.791", 670.7910, Unit::Nanometer, 1.00000000 },
                { "Li-610.365", "Li I 610.365", 610.3650, Unit::Nanometer, 0.20000000 },
                { "Li-488.132", "Li I 488.132", 488.1320, Unit::Nanometer, 0.01350000 },
                { "Li-467.170", "Li I 467.170", 467.1700, Unit::Nanometer, 0.01050000 },
                { "Li-460.289", "Li I 460.289", 460.2890, Unit::Nanometer, 0.05250000 },
                { "Li-427.313", "Li I 427.313", 427.3130, Unit::Nanometer, 0.01650000 },
            };
            return lines;
        }

        const std::vector<AtomicScaleBuilder::SourceLine>& berylliumLines()
        {
            using Unit = AtomicScaleBuilder::WavelengthUnit;
            static const std::vector<AtomicScaleBuilder::SourceLine> lines {
                { "Be-698.275", "Be I 698.275", 698.2750, Unit::Nanometer, 0.00566667 },
                { "Be-688.444", "Be I 688.444", 688.4440, Unit::Nanometer, 0.05000000 },
                { "Be-678.656", "Be I 678.656", 678.6560, Unit::Nanometer, 0.03333333 },
                { "Be-672.600", "Be I 672.600", 672.6000, Unit::Nanometer, 0.03133333 },
                { "Be-663.644", "Be I 663.644", 663.6440, Unit::Nanometer, 0.01300000 },
                { "Be-654.789", "Be I 654.789", 654.7890, Unit::Nanometer, 0.21666667 },
                { "Be-647.354", "Be I 647.354", 647.3540, Unit::Nanometer, 0.01200000 },
                { "Be-627.973", "Be I 627.973", 627.9730, Unit::Nanometer, 0.00600000 },
                { "Be-622.911", "Be I 622.911", 622.9110, Unit::Nanometer, 0.00733333 },
                { "Be-614.201", "Be I 614.201", 614.2010, Unit::Nanometer, 0.01833333 },
                { "Be-585.701", "Be I 585.701", 585.7010, Unit::Nanometer, 0.00633333 },
                { "Be-540.304", "Be I 540.304", 540.3040, Unit::Nanometer, 0.01666667 },
                { "Be-526.152", "Be I 526.152", 526.1520, Unit::Nanometer, 0.00566667 },
                { "Be-521.833", "Be I 521.833", 521.8330, Unit::Nanometer, 0.02800000 },
                { "Be-515.600", "Be I 515.600", 515.6000, Unit::Nanometer, 0.10000000 },
                { "Be-508.775", "Be I 508.775", 508.7750, Unit::Nanometer, 0.10000000 },
                { "Be-485.822", "Be I 485.822", 485.8220, Unit::Nanometer, 0.01066667 },
                { "Be-480.778", "Be I 480.778", 480.7780, Unit::Nanometer, 0.03266667 },
                { "Be-467.342", "Be I 467.342", 467.3420, Unit::Nanometer, 0.55000000 },
                { "Be-457.267", "Be I 457.267", 457.2670, Unit::Nanometer, 0.55000000 },
                { "Be-452.641", "Be I 452.641", 452.6410, Unit::Nanometer, 0.02866667 },
                { "Be-446.778", "Be I 446.778", 446.7780, Unit::Nanometer, 0.03333333 },
                { "Be-437.110", "Be I 437.110", 437.1100, Unit::Nanometer, 0.03333333 },
                { "Be-432.955", "Be I 432.955", 432.9550, Unit::Nanometer, 0.01866667 },
                { "Be-424.914", "Be I 424.914", 424.9140, Unit::Nanometer, 0.03333333 },
                { "Be-399.550", "Be I 399.550", 399.5500, Unit::Nanometer, 0.01433333 },
                { "Be-386.551", "Be I 386.551", 386.5510, Unit::Nanometer, 0.33333333 },
            };
            return lines;
        }

        const std::vector<AtomicScaleBuilder::SourceLine>& boronLines()
        {
            using Unit = AtomicScaleBuilder::WavelengthUnit;
            static const std::vector<AtomicScaleBuilder::SourceLine> lines {
                { "B-628.547", "B I 628.547", 628.5470, Unit::Nanometer, 0.00266667 },
                { "B-614.892", "B I 614.892", 614.8920, Unit::Nanometer, 0.00355556 },
                { "B-608.044", "B I 608.044", 608.0440, Unit::Nanometer, 0.00755556 },
                { "B-581.840", "B I 581.840", 581.8400, Unit::Nanometer, 0.00186667 },
                { "B-563.327", "B I 563.327", 563.3270, Unit::Nanometer, 0.01688889 },
                { "B-556.319", "B I 556.319", 556.3190, Unit::Nanometer, 0.00711111 },
                { "B-550.456", "B I 550.456", 550.4560, Unit::Nanometer, 0.00240000 },
                { "B-539.317", "B I 539.317", 539.3170, Unit::Nanometer, 0.00666667 },
                { "B-534.765", "B I 534.765", 534.7650, Unit::Nanometer, 0.00133333 },
                { "B-522.686", "B I 522.686", 522.6860, Unit::Nanometer, 0.00208889 },
                { "B-512.579", "B I 512.579", 512.5790, Unit::Nanometer, 0.00444444 },
                { "B-498.866", "B I 498.866", 498.8660, Unit::Nanometer, 0.00622222 },
                { "B-491.843", "B I 491.843", 491.8430, Unit::Nanometer, 0.04444444 },
                { "B-477.330", "B I 477.330", 477.3300, Unit::Nanometer, 0.00533333 },
                { "B-471.970", "B I 471.970", 471.9700, Unit::Nanometer, 0.00711111 },
                { "B-464.713", "B I 464.713", 464.7130, Unit::Nanometer, 0.03022222 },
                { "B-459.730", "B I 459.730", 459.7300, Unit::Nanometer, 0.00240000 },
                { "B-448.705", "B I 448.705", 448.7050, Unit::Nanometer, 0.20000000 },
                { "B-443.150", "B I 443.150", 443.1500, Unit::Nanometer, 0.00142222 },
                { "B-436.611", "B I 436.611", 436.6110, Unit::Nanometer, 0.01022222 },
                { "B-429.571", "B I 429.571", 429.5710, Unit::Nanometer, 0.00533333 },
                { "B-424.361", "B I 424.361", 424.3610, Unit::Nanometer, 0.06666667 },
                { "B-419.777", "B I 419.777", 419.7770, Unit::Nanometer, 0.00244444 },
                { "B-412.193", "B I 412.193", 412.1930, Unit::Nanometer, 1.00000000 },
                { "B-399.024", "B I 399.024", 399.0240, Unit::Nanometer, 0.00800000 },
                { "B-394.628", "B I 394.628", 394.6280, Unit::Nanometer, 0.00168889 },
            };
            return lines;
        }

        const std::vector<AtomicScaleBuilder::SourceLine>& carbonLines()
        {
            using Unit = AtomicScaleBuilder::WavelengthUnit;
            static const std::vector<AtomicScaleBuilder::SourceLine> lines {
                { "C-694.645", "C I 694.645", 694.6450, Unit::Nanometer, 1.00000000 },
                { "C-678.390", "C I 678.390", 678.3900, Unit::Nanometer, 0.39000000 },
                { "C-671.129", "C I 671.129", 671.1290, Unit::Nanometer, 0.01500000 },
                { "C-666.304", "C I 666.304", 666.3040, Unit::Nanometer, 0.13500000 },
                { "C-661.724", "C I 661.724", 661.7240, Unit::Nanometer, 0.00750000 },
                { "C-656.871", "C I 656.871", 656.8710, Unit::Nanometer, 0.00495000 },
                { "C-639.798", "C I 639.798", 639.7980, Unit::Nanometer, 0.23250000 },
                { "C-634.232", "C I 634.232", 634.2320, Unit::Nanometer, 0.31500000 },
                { "C-629.237", "C I 629.237", 629.2370, Unit::Nanometer, 0.50000000 },
                { "C-623.727", "C I 623.727", 623.7270, Unit::Nanometer, 0.18000000 },
                { "C-609.430", "C I 609.430", 609.4300, Unit::Nanometer, 0.02000000 },
                { "C-604.479", "C I 604.479", 604.4790, Unit::Nanometer, 0.01000000 },
                { "C-599.606", "C I 599.606", 599.6060, Unit::Nanometer, 0.01250000 },
                { "C-594.339", "C I 594.339", 594.3390, Unit::Nanometer, 0.00415000 },
                { "C-589.200", "C I 589.200", 589.2000, Unit::Nanometer, 0.00115000 },
                { "C-584.635", "C I 584.635", 584.6350, Unit::Nanometer, 0.00375000 },
                { "C-578.784", "C I 578.784", 578.7840, Unit::Nanometer, 0.00500000 },
                { "C-572.078", "C I 572.078", 572.0780, Unit::Nanometer, 0.02250000 },
                { "C-566.895", "C I 566.895", 566.8950, Unit::Nanometer, 0.09000000 },
                { "C-560.373", "C I 560.373", 560.3730, Unit::Nanometer, 0.00750000 },
                { "C-555.159", "C I 555.159", 555.1590, Unit::Nanometer, 0.09750000 },
                { "C-550.851", "C I 550.851", 550.8510, Unit::Nanometer, 0.03000000 },
                { "C-543.688", "C I 543.688", 543.6880, Unit::Nanometer, 0.31500000 },
                { "C-538.033", "C I 538.033", 538.0330, Unit::Nanometer, 0.24000000 },
                { "C-531.747", "C I 531.747", 531.7470, Unit::Nanometer, 0.05250000 },
                { "C-526.896", "C I 526.896", 526.8960, Unit::Nanometer, 0.12750000 },
                { "C-513.328", "C I 513.328", 513.3280, Unit::Nanometer, 0.12000000 },
                { "C-504.180", "C I 504.180", 504.1800, Unit::Nanometer, 0.14250000 },
                { "C-499.643", "C I 499.643", 499.6430, Unit::Nanometer, 0.04250000 },
                { "C-492.642", "C I 492.642", 492.6420, Unit::Nanometer, 0.00435000 },
                { "C-487.408", "C I 487.408", 487.4080, Unit::Nanometer, 0.01750000 },
                { "C-481.737", "C I 481.737", 481.7370, Unit::Nanometer, 0.02750000 },
                { "C-476.253", "C I 476.253", 476.2530, Unit::Nanometer, 0.08250000 },
                { "C-470.383", "C I 470.383", 470.3830, Unit::Nanometer, 0.00750000 },
                { "C-464.742", "C I 464.742", 464.7420, Unit::Nanometer, 0.00250000 },
                { "C-446.445", "C I 446.445", 446.4450, Unit::Nanometer, 0.02500000 },
                { "C-437.138", "C I 437.138", 437.1380, Unit::Nanometer, 0.09000000 },
                { "C-432.556", "C I 432.556", 432.5560, Unit::Nanometer, 0.00135000 },
                { "C-426.726", "C I 426.726", 426.7260, Unit::Nanometer, 0.18750000 },
                { "C-420.990", "C I 420.990", 420.9900, Unit::Nanometer, 0.00440000 },
                { "C-414.697", "C I 414.697", 414.6970, Unit::Nanometer, 0.00500000 },
                { "C-408.298", "C I 408.298", 408.2980, Unit::Nanometer, 0.00750000 },
                { "C-403.323", "C I 403.323", 403.3230, Unit::Nanometer, 0.00500000 },
                { "C-398.688", "C I 398.688", 398.6880, Unit::Nanometer, 0.00750000 },
                { "C-392.069", "C I 392.069", 392.0690, Unit::Nanometer, 0.29250000 },
                { "C-387.641", "C I 387.641", 387.6410, Unit::Nanometer, 0.09750000 },
                { "C-382.885", "C I 382.885", 382.8850, Unit::Nanometer, 0.01250000 },
            };
            return lines;
        }

        const std::vector<AtomicScaleBuilder::SourceLine>& oxygenLines()
        {
            using Unit = AtomicScaleBuilder::WavelengthUnit;
            static const std::vector<AtomicScaleBuilder::SourceLine> lines {
                { "O-689.511", "O I 689.511", 689.5110, Unit::Nanometer, 0.05500000 },
                { "O-672.140", "O I 672.140", 672.1400, Unit::Nanometer, 0.03166667 },
                { "O-660.491", "O I 660.491", 660.4910, Unit::Nanometer, 0.02833333 },
                { "O-655.462", "O I 655.462", 655.4620, Unit::Nanometer, 0.01333333 },
                { "O-650.219", "O I 650.219", 650.2190, Unit::Nanometer, 0.00833333 },
                { "O-645.444", "O I 645.444", 645.4440, Unit::Nanometer, 0.12000000 },
                { "O-636.634", "O I 636.634", 636.6340, Unit::Nanometer, 0.08000000 },
                { "O-615.677", "O I 615.677", 615.6770, Unit::Nanometer, 0.28000000 },
                { "O-610.627", "O I 610.627", 610.6270, Unit::Nanometer, 0.50000000 },
                { "O-604.644", "O I 604.644", 604.6440, Unit::Nanometer, 0.33333333 },
                { "O-595.858", "O I 595.858", 595.8580, Unit::Nanometer, 0.25000000 },
                { "O-557.734", "O I 557.734", 557.7340, Unit::Nanometer, 0.03000000 },
                { "O-543.578", "O I 543.578", 543.5780, Unit::Nanometer, 0.05000000 },
                { "O-537.757", "O I 537.757", 537.7570, Unit::Nanometer, 0.00203333 },
                { "O-532.968", "O I 532.968", 532.9680, Unit::Nanometer, 0.09500000 },
                { "O-520.665", "O I 520.665", 520.6650, Unit::Nanometer, 0.08000000 },
                { "O-515.994", "O I 515.994", 515.9940, Unit::Nanometer, 0.26000000 },
                { "O-495.571", "O I 495.571", 495.5710, Unit::Nanometer, 0.04500000 },
                { "O-490.683", "O I 490.683", 490.6830, Unit::Nanometer, 0.08500000 },
                { "O-485.676", "O I 485.676", 485.6760, Unit::Nanometer, 0.08000000 },
                { "O-474.171", "O I 474.171", 474.1710, Unit::Nanometer, 0.01833333 },
                { "O-469.900", "O I 469.900", 469.9000, Unit::Nanometer, 0.02500000 },
                { "O-464.913", "O I 464.913", 464.9130, Unit::Nanometer, 0.33333333 },
                { "O-459.618", "O I 459.618", 459.6180, Unit::Nanometer, 0.20000000 },
                { "O-446.792", "O I 446.792", 446.7920, Unit::Nanometer, 0.05500000 },
                { "O-441.697", "O I 441.697", 441.6970, Unit::Nanometer, 0.14500000 },
                { "O-435.126", "O I 435.126", 435.1260, Unit::Nanometer, 0.12500000 },
                { "O-430.382", "O I 430.382", 430.3820, Unit::Nanometer, 0.06000000 },
                { "O-425.390", "O I 425.390", 425.3900, Unit::Nanometer, 0.10500000 },
                { "O-419.252", "O I 419.252", 419.2520, Unit::Nanometer, 0.00833333 },
                { "O-414.608", "O I 414.608", 414.6080, Unit::Nanometer, 0.04500000 },
                { "O-409.652", "O I 409.652", 409.6520, Unit::Nanometer, 0.01166667 },
                { "O-404.821", "O I 404.821", 404.8210, Unit::Nanometer, 0.00280000 },
                { "O-395.461", "O I 395.461", 395.4610, Unit::Nanometer, 0.02500000 },
                { "O-390.746", "O I 390.746", 390.7460, Unit::Nanometer, 0.01333333 },
                { "O-385.147", "O I 385.147", 385.1470, Unit::Nanometer, 0.00160000 },
                { "O-380.299", "O I 380.299", 380.2990, Unit::Nanometer, 0.01666667 },
            };
            return lines;
        }

        const std::vector<AtomicScaleBuilder::SourceLine>& fluorineLines()
        {
            using Unit = AtomicScaleBuilder::WavelengthUnit;
            static const std::vector<AtomicScaleBuilder::SourceLine> lines {
                { "F-696.635", "F I 696.635", 696.6350, Unit::Nanometer, 0.06857143 },
                { "F-690.982", "F I 690.982", 690.9820, Unit::Nanometer, 0.14571429 },
                { "F-683.426", "F I 683.426", 683.4260, Unit::Nanometer, 0.28571429 },
                { "F-677.398", "F I 677.398", 677.3980, Unit::Nanometer, 0.09857143 },
                { "F-670.828", "F I 670.828", 670.8280, Unit::Nanometer, 0.03857143 },
                { "F-665.041", "F I 665.041", 665.0410, Unit::Nanometer, 0.03857143 },
                { "F-656.969", "F I 656.969", 656.9690, Unit::Nanometer, 0.04714286 },
                { "F-646.350", "F I 646.350", 646.3500, Unit::Nanometer, 0.01428571 },
                { "F-641.365", "F I 641.365", 641.3650, Unit::Nanometer, 0.14571429 },
                { "F-634.851", "F I 634.851", 634.8510, Unit::Nanometer, 0.19714286 },
                { "F-621.087", "F I 621.087", 621.0870, Unit::Nanometer, 0.00428571 },
                { "F-614.976", "F I 614.976", 614.9760, Unit::Nanometer, 0.01000000 },
                { "F-608.011", "F I 608.011", 608.0110, Unit::Nanometer, 0.00194286 },
                { "F-601.583", "F I 601.583", 601.5830, Unit::Nanometer, 0.00194286 },
                { "F-567.167", "F I 567.167", 567.1670, Unit::Nanometer, 0.00194286 },
                { "F-517.325", "F I 517.325", 517.3250, Unit::Nanometer, 0.01428571 },
                { "F-511.099", "F I 511.099", 511.0990, Unit::Nanometer, 0.00428571 },
                { "F-500.200", "F I 500.200", 500.2000, Unit::Nanometer, 0.03000000 },
                { "F-493.326", "F I 493.326", 493.3260, Unit::Nanometer, 0.03000000 },
                { "F-485.939", "F I 485.939", 485.9390, Unit::Nanometer, 0.04714286 },
                { "F-473.438", "F I 473.438", 473.4380, Unit::Nanometer, 0.00265714 },
                { "F-444.672", "F I 444.672", 444.6720, Unit::Nanometer, 0.05571429 },
                { "F-427.753", "F I 427.753", 427.7530, Unit::Nanometer, 0.00714286 },
                { "F-420.715", "F I 420.715", 420.7150, Unit::Nanometer, 0.00714286 },
                { "F-408.391", "F I 408.391", 408.3910, Unit::Nanometer, 0.00571429 },
                { "F-402.501", "F I 402.501", 402.5010, Unit::Nanometer, 0.01857143 },
                { "F-397.478", "F I 397.478", 397.4780, Unit::Nanometer, 0.00234286 },
                { "F-389.883", "F I 389.883", 389.8830, Unit::Nanometer, 0.00240000 },
                { "F-384.999", "F I 384.999", 384.9990, Unit::Nanometer, 0.07714286 },
            };
            return lines;
        }

        const std::vector<AtomicScaleBuilder::SourceLine>& neonLines()
        {
            using Unit = AtomicScaleBuilder::WavelengthUnit;
            static const std::vector<AtomicScaleBuilder::SourceLine> lines {
                { "Ne-688.694", "Ne I 688.694", 688.6940, Unit::Nanometer, 0.01600000 },
                { "Ne-671.704", "Ne I 671.704", 671.7040, Unit::Nanometer, 0.01800000 },
                { "Ne-665.209", "Ne I 665.209", 665.2090, Unit::Nanometer, 0.20000000 },
                { "Ne-660.290", "Ne I 660.290", 660.2900, Unit::Nanometer, 0.02800000 },
                { "Ne-655.902", "Ne I 655.902", 655.9020, Unit::Nanometer, 0.02600000 },
                { "Ne-650.653", "Ne I 650.653", 650.6530, Unit::Nanometer, 0.26666667 },
                { "Ne-640.658", "Ne I 640.658", 640.6580, Unit::Nanometer, 0.04600000 },
                { "Ne-634.565", "Ne I 634.565", 634.5650, Unit::Nanometer, 0.01400000 },
                { "Ne-629.784", "Ne I 629.784", 629.7840, Unit::Nanometer, 0.03200000 },
                { "Ne-624.959", "Ne I 624.959", 624.9590, Unit::Nanometer, 0.00130667 },
                { "Ne-619.307", "Ne I 619.307", 619.3070, Unit::Nanometer, 0.01133333 },
                { "Ne-614.306", "Ne I 614.306", 614.3060, Unit::Nanometer, 0.46666667 },
                { "Ne-609.616", "Ne I 609.616", 609.6160, Unit::Nanometer, 0.40000000 },
                { "Ne-603.000", "Ne I 603.000", 603.0000, Unit::Nanometer, 0.13000000 },
                { "Ne-596.547", "Ne I 596.547", 596.5470, Unit::Nanometer, 0.07400000 },
                { "Ne-591.891", "Ne I 591.891", 591.8910, Unit::Nanometer, 0.03000000 },
                { "Ne-586.842", "Ne I 586.842", 586.8420, Unit::Nanometer, 0.02000000 },
                { "Ne-581.662", "Ne I 581.662", 581.6620, Unit::Nanometer, 0.00266667 },
                { "Ne-576.442", "Ne I 576.442", 576.4420, Unit::Nanometer, 0.01800000 },
                { "Ne-571.922", "Ne I 571.922", 571.9220, Unit::Nanometer, 0.01333333 },
                { "Ne-565.603", "Ne I 565.603", 565.6030, Unit::Nanometer, 0.00200000 },
                { "Ne-556.277", "Ne I 556.277", 556.2770, Unit::Nanometer, 0.01333333 },
                { "Ne-549.442", "Ne I 549.442", 549.4420, Unit::Nanometer, 0.00130667 },
                { "Ne-542.016", "Ne I 542.016", 542.0160, Unit::Nanometer, 0.00130667 },
                { "Ne-537.664", "Ne I 537.664", 537.6640, Unit::Nanometer, 0.00200000 },
                { "Ne-532.640", "Ne I 532.640", 532.6400, Unit::Nanometer, 0.00133333 },
                { "Ne-526.312", "Ne I 526.312", 526.3120, Unit::Nanometer, 0.00200000 },
                { "Ne-519.756", "Ne I 519.756", 519.7560, Unit::Nanometer, 0.00133333 },
                { "Ne-514.494", "Ne I 514.494", 514.4940, Unit::Nanometer, 0.00933333 },
                { "Ne-508.038", "Ne I 508.038", 508.0380, Unit::Nanometer, 0.00266667 },
                { "Ne-502.287", "Ne I 502.287", 502.2870, Unit::Nanometer, 0.00200000 },
                { "Ne-497.356", "Ne I 497.356", 497.3560, Unit::Nanometer, 0.00133333 },
                { "Ne-492.823", "Ne I 492.823", 492.8230, Unit::Nanometer, 0.00100000 },
                { "Ne-486.648", "Ne I 486.648", 486.6480, Unit::Nanometer, 0.00118667 },
                { "Ne-481.764", "Ne I 481.764", 481.7640, Unit::Nanometer, 0.00466667 },
                { "Ne-475.444", "Ne I 475.444", 475.4440, Unit::Nanometer, 0.00133333 },
                { "Ne-470.439", "Ne I 470.439", 470.4390, Unit::Nanometer, 0.02400000 },
                { "Ne-465.970", "Ne I 465.970", 465.9700, Unit::Nanometer, 0.00133333 },
                { "Ne-460.991", "Ne I 460.991", 460.9910, Unit::Nanometer, 0.00200000 },
                { "Ne-455.340", "Ne I 455.340", 455.3400, Unit::Nanometer, 0.00122667 },
                { "Ne-449.624", "Ne I 449.624", 449.6240, Unit::Nanometer, 0.00104000 },
                { "Ne-444.080", "Ne I 444.080", 444.0800, Unit::Nanometer, 0.00133333 },
                { "Ne-439.631", "Ne I 439.631", 439.6310, Unit::Nanometer, 0.00200000 },
                { "Ne-434.624", "Ne I 434.624", 434.6240, Unit::Nanometer, 0.00333333 },
                { "Ne-429.060", "Ne I 429.060", 429.0600, Unit::Nanometer, 0.00333333 },
                { "Ne-424.204", "Ne I 424.204", 424.2040, Unit::Nanometer, 0.00120000 },
                { "Ne-419.959", "Ne I 419.959", 419.9590, Unit::Nanometer, 0.00200000 },
                { "Ne-413.387", "Ne I 413.387", 413.3870, Unit::Nanometer, 0.00133333 },
                { "Ne-408.442", "Ne I 408.442", 408.4420, Unit::Nanometer, 0.00266667 },
                { "Ne-402.404", "Ne I 402.404", 402.4040, Unit::Nanometer, 0.00200000 },
                { "Ne-396.562", "Ne I 396.562", 396.5620, Unit::Nanometer, 0.00533333 },
                { "Ne-391.724", "Ne I 391.724", 391.7240, Unit::Nanometer, 0.00533333 },
                { "Ne-386.450", "Ne I 386.450", 386.4500, Unit::Nanometer, 0.00400000 },
                { "Ne-381.789", "Ne I 381.789", 381.7890, Unit::Nanometer, 0.00533333 },
            };
            return lines;
        }

        const std::vector<AtomicScaleBuilder::SourceLine>& sodiumLines()
        {
            using Unit = AtomicScaleBuilder::WavelengthUnit;
            static const std::vector<AtomicScaleBuilder::SourceLine> lines {
                { "Na-412.308", "Na I 412.308", 412.3080, Unit::Nanometer, 0.00300000 },
                { "Na-432.091", "Na I 432.091", 432.0910, Unit::Nanometer, 0.00250000 },
                { "Na-439.334", "Na I 439.334", 439.3340, Unit::Nanometer, 0.00450000 },
                { "Na-449.766", "Na I 449.766", 449.7660, Unit::Nanometer, 0.03300000 },
                { "Na-454.519", "Na I 454.519", 454.5190, Unit::Nanometer, 0.00850000 },
                { "Na-466.856", "Na I 466.856", 466.8560, Unit::Nanometer, 0.07650000 },
                { "Na-474.794", "Na I 474.794", 474.7940, Unit::Nanometer, 0.01950000 },
                { "Na-497.854", "Na I 497.854", 497.8540, Unit::Nanometer, 0.00450000 },
                { "Na-568.820", "Na I 568.820", 568.8200, Unit::Nanometer, 0.05850000 },
                { "Na-588.995", "Na I 588.995", 588.9950, Unit::Nanometer, 1.00000000 },
                { "Na-615.423", "Na I 615.423", 615.4230, Unit::Nanometer, 0.06600000 },
            };
            return lines;
        }

        const std::vector<AtomicScaleBuilder::SourceLine>& magnesiumLines()
        {
            using Unit = AtomicScaleBuilder::WavelengthUnit;
            static const std::vector<AtomicScaleBuilder::SourceLine> lines {
                { "Mg-380.741", "Mg I 380.741", 380.7410, Unit::Nanometer, 0.00300000 },
                { "Mg-385.496", "Mg I 385.496", 385.4960, Unit::Nanometer, 0.00300000 },
                { "Mg-390.386", "Mg I 390.386", 390.3860, Unit::Nanometer, 0.02700000 },
                { "Mg-397.574", "Mg I 397.574", 397.5740, Unit::Nanometer, 0.00700000 },
                { "Mg-402.678", "Mg I 402.678", 402.6780, Unit::Nanometer, 0.00600000 },
                { "Mg-407.506", "Mg I 407.506", 407.5060, Unit::Nanometer, 0.00200000 },
                { "Mg-414.310", "Mg I 414.310", 414.3100, Unit::Nanometer, 0.01700000 },
                { "Mg-419.348", "Mg I 419.348", 419.3480, Unit::Nanometer, 0.14100000 },
                { "Mg-424.254", "Mg I 424.254", 424.2540, Unit::Nanometer, 0.00800000 },
                { "Mg-429.636", "Mg I 429.636", 429.6360, Unit::Nanometer, 0.00178000 },
                { "Mg-435.814", "Mg I 435.814", 435.8140, Unit::Nanometer, 0.00900000 },
                { "Mg-440.992", "Mg I 440.992", 440.9920, Unit::Nanometer, 0.00174000 },
                { "Mg-445.210", "Mg I 445.210", 445.2100, Unit::Nanometer, 0.00172000 },
                { "Mg-450.179", "Mg I 450.179", 450.1790, Unit::Nanometer, 0.00156000 },
                { "Mg-455.290", "Mg I 455.290", 455.2900, Unit::Nanometer, 0.00200000 },
                { "Mg-461.680", "Mg I 461.680", 461.6800, Unit::Nanometer, 0.00168000 },
                { "Mg-466.620", "Mg I 466.620", 466.6200, Unit::Nanometer, 0.00168000 },
                { "Mg-472.320", "Mg I 472.320", 472.3200, Unit::Nanometer, 0.06600000 },
                { "Mg-478.560", "Mg I 478.560", 478.5600, Unit::Nanometer, 0.00168000 },
                { "Mg-483.420", "Mg I 483.420", 483.4200, Unit::Nanometer, 0.00170000 },
                { "Mg-488.904", "Mg I 488.904", 488.9040, Unit::Nanometer, 0.00300000 },
                { "Mg-493.940", "Mg I 493.940", 493.9400, Unit::Nanometer, 0.00400000 },
                { "Mg-498.145", "Mg I 498.145", 498.1450, Unit::Nanometer, 0.05100000 },
                { "Mg-505.600", "Mg I 505.600", 505.6000, Unit::Nanometer, 0.00200000 },
                { "Mg-513.200", "Mg I 513.200", 513.2000, Unit::Nanometer, 0.00200000 },
                { "Mg-518.360", "Mg I 518.360", 518.3600, Unit::Nanometer, 1.00000000 },
                { "Mg-526.422", "Mg I 526.422", 526.4220, Unit::Nanometer, 0.00144000 },
                { "Mg-534.598", "Mg I 534.598", 534.5980, Unit::Nanometer, 0.00200000 },
                { "Mg-539.810", "Mg I 539.810", 539.8100, Unit::Nanometer, 0.00128000 },
                { "Mg-545.536", "Mg I 545.536", 545.5360, Unit::Nanometer, 0.00200000 },
                { "Mg-550.960", "Mg I 550.960", 550.9600, Unit::Nanometer, 0.00500000 },
                { "Mg-562.641", "Mg I 562.641", 562.6410, Unit::Nanometer, 0.01600000 },
                { "Mg-569.600", "Mg I 569.600", 569.6000, Unit::Nanometer, 0.00182000 },
                { "Mg-574.122", "Mg I 574.122", 574.1220, Unit::Nanometer, 0.01400000 },
                { "Mg-580.200", "Mg I 580.200", 580.2000, Unit::Nanometer, 0.00136000 },
                { "Mg-589.000", "Mg I 589.000", 589.0000, Unit::Nanometer, 0.00200000 },
                { "Mg-594.587", "Mg I 594.587", 594.5870, Unit::Nanometer, 0.00400000 },
                { "Mg-609.291", "Mg I 609.291", 609.2910, Unit::Nanometer, 0.00800000 },
                { "Mg-615.008", "Mg I 615.008", 615.0080, Unit::Nanometer, 0.06900000 },
                { "Mg-620.844", "Mg I 620.844", 620.8440, Unit::Nanometer, 0.01900000 },
                { "Mg-625.675", "Mg I 625.675", 625.6750, Unit::Nanometer, 0.01600000 },
                { "Mg-631.924", "Mg I 631.924", 631.9240, Unit::Nanometer, 0.00400000 },
                { "Mg-636.000", "Mg I 636.000", 636.0000, Unit::Nanometer, 0.00140000 },
                { "Mg-641.704", "Mg I 641.704", 641.7040, Unit::Nanometer, 0.00300000 },
                { "Mg-646.900", "Mg I 646.900", 646.9000, Unit::Nanometer, 0.00200000 },
                { "Mg-651.143", "Mg I 651.143", 651.1430, Unit::Nanometer, 0.00700000 },
                { "Mg-662.052", "Mg I 662.052", 662.0520, Unit::Nanometer, 0.00800000 },
                { "Mg-674.700", "Mg I 674.700", 674.7000, Unit::Nanometer, 0.00200000 },
                { "Mg-681.927", "Mg I 681.927", 681.9270, Unit::Nanometer, 0.01600000 },
                { "Mg-689.490", "Mg I 689.490", 689.4900, Unit::Nanometer, 0.00800000 },
                { "Mg-694.400", "Mg I 694.400", 694.4000, Unit::Nanometer, 0.00200000 },
            };
            return lines;
        }

        const std::vector<AtomicScaleBuilder::SourceLine>& aluminiumLines()
        {
            using Unit = AtomicScaleBuilder::WavelengthUnit;
            static const std::vector<AtomicScaleBuilder::SourceLine> lines {
                { "Al-384.221", "Al I 384.221", 384.2210, Unit::Nanometer, 0.00266667 },
                { "Al-390.068", "Al I 390.068", 390.0680, Unit::Nanometer, 0.06400000 },
                { "Al-396.152", "Al I 396.152", 396.1520, Unit::Nanometer, 1.00000000 },
                { "Al-402.650", "Al I 402.650", 402.6500, Unit::Nanometer, 0.01266667 },
                { "Al-408.861", "Al I 408.861", 408.8610, Unit::Nanometer, 0.00600000 },
                { "Al-414.992", "Al I 414.992", 414.9920, Unit::Nanometer, 0.13333333 },
                { "Al-419.041", "Al I 419.041", 419.0410, Unit::Nanometer, 0.01600000 },
                { "Al-424.071", "Al I 424.071", 424.0710, Unit::Nanometer, 0.01600000 },
                { "Al-429.394", "Al I 429.394", 429.3940, Unit::Nanometer, 0.00866667 },
                { "Al-434.692", "Al I 434.692", 434.6920, Unit::Nanometer, 0.01600000 },
                { "Al-440.000", "Al I 440.000", 440.0000, Unit::Nanometer, 0.02000000 },
                { "Al-446.885", "Al I 446.885", 446.8850, Unit::Nanometer, 0.04800000 },
                { "Al-451.256", "Al I 451.256", 451.2560, Unit::Nanometer, 0.06000000 },
                { "Al-456.800", "Al I 456.800", 456.8000, Unit::Nanometer, 0.00266667 },
                { "Al-461.101", "Al I 461.101", 461.1010, Unit::Nanometer, 0.00666667 },
                { "Al-466.680", "Al I 466.680", 466.6800, Unit::Nanometer, 0.00200000 },
                { "Al-471.000", "Al I 471.000", 471.0000, Unit::Nanometer, 0.01800000 },
                { "Al-487.000", "Al I 487.000", 487.0000, Unit::Nanometer, 0.05200000 },
                { "Al-495.000", "Al I 495.000", 495.0000, Unit::Nanometer, 0.00600000 },
                { "Al-500.097", "Al I 500.097", 500.0970, Unit::Nanometer, 0.08400000 },
                { "Al-508.502", "Al I 508.502", 508.5020, Unit::Nanometer, 0.06200000 },
                { "Al-514.494", "Al I 514.494", 514.4940, Unit::Nanometer, 0.20000000 },
                { "Al-521.000", "Al I 521.000", 521.0000, Unit::Nanometer, 0.00200000 },
                { "Al-526.011", "Al I 526.011", 526.0110, Unit::Nanometer, 0.00400000 },
                { "Al-531.232", "Al I 531.232", 531.2320, Unit::Nanometer, 0.01266667 },
                { "Al-537.184", "Al I 537.184", 537.1840, Unit::Nanometer, 0.03400000 },
                { "Al-551.853", "Al I 551.853", 551.8530, Unit::Nanometer, 0.00733333 },
                { "Al-559.330", "Al I 559.330", 559.3300, Unit::Nanometer, 0.13333333 },
                { "Al-569.660", "Al I 569.660", 569.6600, Unit::Nanometer, 0.03200000 },
                { "Al-574.600", "Al I 574.600", 574.6000, Unit::Nanometer, 0.00333333 },
                { "Al-585.362", "Al I 585.362", 585.3620, Unit::Nanometer, 0.05800000 },
                { "Al-597.198", "Al I 597.198", 597.1980, Unit::Nanometer, 0.00733333 },
                { "Al-605.984", "Al I 605.984", 605.9840, Unit::Nanometer, 0.00101333 },
                { "Al-618.168", "Al I 618.168", 618.1680, Unit::Nanometer, 0.01200000 },
                { "Al-623.175", "Al I 623.175", 623.1750, Unit::Nanometer, 0.06400000 },
                { "Al-633.570", "Al I 633.570", 633.5700, Unit::Nanometer, 0.01600000 },
                { "Al-638.000", "Al I 638.000", 638.0000, Unit::Nanometer, 0.00533333 },
                { "Al-649.396", "Al I 649.396", 649.3960, Unit::Nanometer, 0.00533333 },
                { "Al-654.312", "Al I 654.312", 654.3120, Unit::Nanometer, 0.20000000 },
                { "Al-660.964", "Al I 660.964", 660.9640, Unit::Nanometer, 0.04800000 },
                { "Al-669.867", "Al I 669.867", 669.8670, Unit::Nanometer, 0.01200000 },
                { "Al-681.669", "Al I 681.669", 681.6690, Unit::Nanometer, 0.00333333 },
                { "Al-690.640", "Al I 690.640", 690.6400, Unit::Nanometer, 0.00266667 },
            };
            return lines;
        }

        const std::vector<AtomicScaleBuilder::SourceLine>& siliconLines()
        {
            using Unit = AtomicScaleBuilder::WavelengthUnit;
            static const std::vector<AtomicScaleBuilder::SourceLine> lines {
                { "Si-380.678", "Si I 380.678", 380.6780, Unit::Nanometer, 0.00345000 },
                { "Si-385.366", "Si I 385.366", 385.3660, Unit::Nanometer, 0.28500000 },
                { "Si-390.552", "Si I 390.552", 390.5520, Unit::Nanometer, 0.09000000 },
                { "Si-395.574", "Si I 395.574", 395.5740, Unit::Nanometer, 0.05250000 },
                { "Si-401.024", "Si I 401.024", 401.0240, Unit::Nanometer, 0.01000000 },
                { "Si-406.538", "Si I 406.538", 406.5380, Unit::Nanometer, 0.03500000 },
                { "Si-411.167", "Si I 411.167", 411.1670, Unit::Nanometer, 0.02500000 },
                { "Si-416.626", "Si I 416.626", 416.6260, Unit::Nanometer, 0.01750000 },
                { "Si-421.210", "Si I 421.210", 421.2100, Unit::Nanometer, 0.02250000 },
                { "Si-427.848", "Si I 427.848", 427.8480, Unit::Nanometer, 0.06000000 },
                { "Si-432.818", "Si I 432.818", 432.8180, Unit::Nanometer, 0.06750000 },
                { "Si-437.622", "Si I 437.622", 437.6220, Unit::Nanometer, 0.02750000 },
                { "Si-442.815", "Si I 442.815", 442.8150, Unit::Nanometer, 0.01250000 },
                { "Si-448.634", "Si I 448.634", 448.6340, Unit::Nanometer, 0.06000000 },
                { "Si-455.400", "Si I 455.400", 455.4000, Unit::Nanometer, 0.07500000 },
                { "Si-460.258", "Si I 460.258", 460.2580, Unit::Nanometer, 0.00750000 },
                { "Si-465.692", "Si I 465.692", 465.6920, Unit::Nanometer, 0.00450000 },
                { "Si-471.665", "Si I 471.665", 471.6650, Unit::Nanometer, 0.03500000 },
                { "Si-476.740", "Si I 476.740", 476.7400, Unit::Nanometer, 0.00750000 },
                { "Si-481.805", "Si I 481.805", 481.8050, Unit::Nanometer, 0.01500000 },
                { "Si-486.907", "Si I 486.907", 486.9070, Unit::Nanometer, 0.01750000 },
                { "Si-491.990", "Si I 491.990", 491.9900, Unit::Nanometer, 0.02250000 },
                { "Si-496.140", "Si I 496.140", 496.1400, Unit::Nanometer, 0.00500000 },
                { "Si-503.640", "Si I 503.640", 503.6400, Unit::Nanometer, 0.17250000 },
                { "Si-509.142", "Si I 509.142", 509.1420, Unit::Nanometer, 0.18000000 },
                { "Si-514.420", "Si I 514.420", 514.4200, Unit::Nanometer, 0.02500000 },
                { "Si-519.726", "Si I 519.726", 519.7260, Unit::Nanometer, 0.00140000 },
                { "Si-529.519", "Si I 529.519", 529.5190, Unit::Nanometer, 0.08250000 },
                { "Si-540.534", "Si I 540.534", 540.5340, Unit::Nanometer, 0.09750000 },
                { "Si-545.449", "Si I 545.449", 545.4490, Unit::Nanometer, 0.00390000 },
                { "Si-550.510", "Si I 550.510", 550.5100, Unit::Nanometer, 0.00130000 },
                { "Si-556.836", "Si I 556.836", 556.8360, Unit::Nanometer, 0.00145000 },
                { "Si-562.222", "Si I 562.222", 562.2220, Unit::Nanometer, 0.00285000 },
                { "Si-568.700", "Si I 568.700", 568.7000, Unit::Nanometer, 0.00125000 },
                { "Si-573.973", "Si I 573.973", 573.9730, Unit::Nanometer, 0.01250000 },
                { "Si-578.573", "Si I 578.573", 578.5730, Unit::Nanometer, 0.01000000 },
                { "Si-584.613", "Si I 584.613", 584.6130, Unit::Nanometer, 0.05250000 },
                { "Si-589.879", "Si I 589.879", 589.8790, Unit::Nanometer, 0.01500000 },
                { "Si-594.854", "Si I 594.854", 594.8540, Unit::Nanometer, 0.04750000 },
                { "Si-606.745", "Si I 606.745", 606.7450, Unit::Nanometer, 0.10500000 },
                { "Si-612.502", "Si I 612.502", 612.5020, Unit::Nanometer, 0.01000000 },
                { "Si-617.361", "Si I 617.361", 617.3610, Unit::Nanometer, 0.00240000 },
                { "Si-623.961", "Si I 623.961", 623.9610, Unit::Nanometer, 0.00175000 },
                { "Si-629.152", "Si I 629.152", 629.1520, Unit::Nanometer, 0.00215000 },
                { "Si-634.710", "Si I 634.710", 634.7100, Unit::Nanometer, 0.50000000 },
                { "Si-647.390", "Si I 647.390", 647.3900, Unit::Nanometer, 0.00480000 },
                { "Si-652.453", "Si I 652.453", 652.4530, Unit::Nanometer, 0.00750000 },
                { "Si-658.961", "Si I 658.961", 658.9610, Unit::Nanometer, 0.00750000 },
                { "Si-663.105", "Si I 663.105", 663.1050, Unit::Nanometer, 0.00400000 },
                { "Si-669.938", "Si I 669.938", 669.9380, Unit::Nanometer, 0.07500000 },
                { "Si-674.670", "Si I 674.670", 674.6700, Unit::Nanometer, 0.00500000 },
                { "Si-680.758", "Si I 680.758", 680.7580, Unit::Nanometer, 0.01750000 },
                { "Si-685.165", "Si I 685.165", 685.1650, Unit::Nanometer, 0.09000000 },
                { "Si-690.930", "Si I 690.930", 690.9300, Unit::Nanometer, 0.00750000 },
                { "Si-696.941", "Si I 696.941", 696.9410, Unit::Nanometer, 0.01000000 },
            };
            return lines;
        }

        const std::vector<AtomicScaleBuilder::SourceLine>& phosphorusLines()
        {
            using Unit = AtomicScaleBuilder::WavelengthUnit;
            static const std::vector<AtomicScaleBuilder::SourceLine> lines {
                { "P-389.506", "P I 389.506", 389.5060, Unit::Nanometer, 0.33333333 },
                { "P-395.764", "P I 395.764", 395.7640, Unit::Nanometer, 0.33333333 },
                { "P-405.931", "P I 405.931", 405.9310, Unit::Nanometer, 0.22222222 },
                { "P-417.848", "P I 417.848", 417.8480, Unit::Nanometer, 0.66666667 },
                { "P-422.220", "P I 422.220", 422.2200, Unit::Nanometer, 0.55555556 },
                { "P-428.860", "P I 428.860", 428.8600, Unit::Nanometer, 0.09333333 },
                { "P-438.535", "P I 438.535", 438.5350, Unit::Nanometer, 0.17666667 },
                { "P-445.246", "P I 445.246", 445.2460, Unit::Nanometer, 0.22222222 },
                { "P-452.000", "P I 452.000", 452.0000, Unit::Nanometer, 0.00180000 },
                { "P-458.804", "P I 458.804", 458.8040, Unit::Nanometer, 0.55555556 },
                { "P-465.831", "P I 465.831", 465.8310, Unit::Nanometer, 0.22000000 },
                { "P-473.655", "P I 473.655", 473.6550, Unit::Nanometer, 0.00222222 },
                { "P-482.370", "P I 482.370", 482.3700, Unit::Nanometer, 0.00222222 },
                { "P-492.720", "P I 492.720", 492.7200, Unit::Nanometer, 0.13000000 },
                { "P-497.815", "P I 497.815", 497.8150, Unit::Nanometer, 0.00333333 },
                { "P-504.541", "P I 504.541", 504.5410, Unit::Nanometer, 0.00333333 },
                { "P-509.822", "P I 509.822", 509.8220, Unit::Nanometer, 0.33333333 },
                { "P-515.484", "P I 515.484", 515.4840, Unit::Nanometer, 0.03333333 },
                { "P-520.401", "P I 520.401", 520.4010, Unit::Nanometer, 0.00333333 },
                { "P-525.841", "P I 525.841", 525.8410, Unit::Nanometer, 0.00333333 },
                { "P-531.607", "P I 531.607", 531.6070, Unit::Nanometer, 0.55555556 },
                { "P-536.463", "P I 536.463", 536.4630, Unit::Nanometer, 0.05000000 },
                { "P-542.809", "P I 542.809", 542.8090, Unit::Nanometer, 0.16000000 },
                { "P-547.786", "P I 547.786", 547.7860, Unit::Nanometer, 0.22222222 },
                { "P-554.852", "P I 554.852", 554.8520, Unit::Nanometer, 0.00333333 },
                { "P-572.771", "P I 572.771", 572.7710, Unit::Nanometer, 0.05000000 },
                { "P-598.977", "P I 598.977", 598.9770, Unit::Nanometer, 0.14333333 },
                { "P-603.404", "P I 603.404", 603.4040, Unit::Nanometer, 0.02666667 },
                { "P-608.782", "P I 608.782", 608.7820, Unit::Nanometer, 0.16666667 },
                { "P-614.260", "P I 614.260", 614.2600, Unit::Nanometer, 0.05000000 },
                { "P-619.902", "P I 619.902", 619.9020, Unit::Nanometer, 0.13333333 },
                { "P-636.727", "P I 636.727", 636.7270, Unit::Nanometer, 0.11000000 },
                { "P-643.631", "P I 643.631", 643.6310, Unit::Nanometer, 0.12000000 },
                { "P-648.638", "P I 648.638", 648.6380, Unit::Nanometer, 0.08000000 },
                { "P-671.591", "P I 671.591", 671.5910, Unit::Nanometer, 0.10333333 },
                { "P-696.918", "P I 696.918", 696.9180, Unit::Nanometer, 0.00222222 },
            };
            return lines;
        }

        const std::vector<AtomicScaleBuilder::SourceLine>& sulfurLines()
        {
            using Unit = AtomicScaleBuilder::WavelengthUnit;
            static const std::vector<AtomicScaleBuilder::SourceLine> lines {
                { "S-380.964", "S I 380.964", 380.9640, Unit::Nanometer, 0.05050505 },
                { "S-385.724", "S I 385.724", 385.7240, Unit::Nanometer, 0.00242424 },
                { "S-390.441", "S I 390.441", 390.4410, Unit::Nanometer, 0.00187879 },
                { "S-395.500", "S I 395.500", 395.5000, Unit::Nanometer, 0.00145455 },
                { "S-400.777", "S I 400.777", 400.7770, Unit::Nanometer, 0.00565657 },
                { "S-405.386", "S I 405.386", 405.3860, Unit::Nanometer, 0.00105051 },
                { "S-412.529", "S I 412.529", 412.5290, Unit::Nanometer, 0.00606061 },
                { "S-417.427", "S I 417.427", 417.4270, Unit::Nanometer, 0.14141414 },
                { "S-422.910", "S I 422.910", 422.9100, Unit::Nanometer, 0.00343434 },
                { "S-427.850", "S I 427.850", 427.8500, Unit::Nanometer, 0.21212121 },
                { "S-433.383", "S I 433.383", 433.3830, Unit::Nanometer, 0.01212121 },
                { "S-438.353", "S I 438.353", 438.3530, Unit::Nanometer, 0.07070707 },
                { "S-443.330", "S I 443.330", 443.3300, Unit::Nanometer, 0.00565657 },
                { "S-448.510", "S I 448.510", 448.5100, Unit::Nanometer, 0.00444444 },
                { "S-453.335", "S I 453.335", 453.3350, Unit::Nanometer, 0.09090909 },
                { "S-458.890", "S I 458.890", 458.8900, Unit::Nanometer, 0.00484848 },
                { "S-463.290", "S I 463.290", 463.2900, Unit::Nanometer, 0.00323232 },
                { "S-468.129", "S I 468.129", 468.1290, Unit::Nanometer, 0.01252525 },
                { "S-473.600", "S I 473.600", 473.6000, Unit::Nanometer, 0.00153535 },
                { "S-479.347", "S I 479.347", 479.3470, Unit::Nanometer, 0.00202020 },
                { "S-484.898", "S I 484.898", 484.8980, Unit::Nanometer, 0.02020202 },
                { "S-489.911", "S I 489.911", 489.9110, Unit::Nanometer, 0.00444444 },
                { "S-494.500", "S I 494.500", 494.5000, Unit::Nanometer, 0.00545455 },
                { "S-499.350", "S I 499.350", 499.3500, Unit::Nanometer, 1.00000000 },
                { "S-504.674", "S I 504.674", 504.6740, Unit::Nanometer, 0.06060606 },
                { "S-509.399", "S I 509.399", 509.3990, Unit::Nanometer, 0.05050505 },
                { "S-514.234", "S I 514.234", 514.2340, Unit::Nanometer, 0.27272727 },
                { "S-519.884", "S I 519.884", 519.8840, Unit::Nanometer, 0.12121212 },
                { "S-524.989", "S I 524.989", 524.9890, Unit::Nanometer, 0.00828283 },
                { "S-529.570", "S I 529.570", 529.5700, Unit::Nanometer, 0.00787879 },
                { "S-534.572", "S I 534.572", 534.5720, Unit::Nanometer, 0.21212121 },
                { "S-539.600", "S I 539.600", 539.6000, Unit::Nanometer, 0.00343434 },
                { "S-545.383", "S I 545.383", 545.3830, Unit::Nanometer, 0.16161616 },
                { "S-550.899", "S I 550.899", 550.8990, Unit::Nanometer, 0.00133333 },
                { "S-555.906", "S I 555.906", 555.9060, Unit::Nanometer, 0.00505051 },
                { "S-560.615", "S I 560.615", 560.6150, Unit::Nanometer, 0.21212121 },
                { "S-565.998", "S I 565.998", 565.9980, Unit::Nanometer, 0.14141414 },
                { "S-570.312", "S I 570.312", 570.3120, Unit::Nanometer, 0.00145455 },
                { "S-575.500", "S I 575.500", 575.5000, Unit::Nanometer, 0.00868687 },
                { "S-581.970", "S I 581.970", 581.9700, Unit::Nanometer, 0.00626263 },
                { "S-586.036", "S I 586.036", 586.0360, Unit::Nanometer, 0.00585859 },
                { "S-591.279", "S I 591.279", 591.2790, Unit::Nanometer, 0.03030303 },
                { "S-596.119", "S I 596.119", 596.1190, Unit::Nanometer, 0.00969697 },
                { "S-601.576", "S I 601.576", 601.5760, Unit::Nanometer, 0.00444444 },
                { "S-606.672", "S I 606.672", 606.6720, Unit::Nanometer, 0.00363636 },
                { "S-611.799", "S I 611.799", 611.7990, Unit::Nanometer, 0.01070707 },
                { "S-616.300", "S I 616.300", 616.3000, Unit::Nanometer, 0.00464646 },
                { "S-621.339", "S I 621.339", 621.3390, Unit::Nanometer, 0.00151515 },
                { "S-627.431", "S I 627.431", 627.4310, Unit::Nanometer, 0.00444444 },
                { "S-636.934", "S I 636.934", 636.9340, Unit::Nanometer, 0.02020202 },
                { "S-641.550", "S I 641.550", 641.5500, Unit::Nanometer, 0.00565657 },
                { "S-647.870", "S I 647.870", 647.8700, Unit::Nanometer, 0.00189899 },
                { "S-652.500", "S I 652.500", 652.5000, Unit::Nanometer, 0.00282828 },
                { "S-659.490", "S I 659.490", 659.4900, Unit::Nanometer, 0.00747475 },
                { "S-664.106", "S I 664.106", 664.1060, Unit::Nanometer, 0.05050505 },
                { "S-670.020", "S I 670.020", 670.0200, Unit::Nanometer, 0.00303030 },
                { "S-675.696", "S I 675.696", 675.6960, Unit::Nanometer, 0.00202020 },
                { "S-680.389", "S I 680.389", 680.3890, Unit::Nanometer, 0.00505051 },
                { "S-685.390", "S I 685.390", 685.3900, Unit::Nanometer, 0.00187879 },
                { "S-690.440", "S I 690.440", 690.4400, Unit::Nanometer, 0.00185859 },
                { "S-695.793", "S I 695.793", 695.7930, Unit::Nanometer, 0.03030303 },
            };
            return lines;
        }

        const std::vector<AtomicScaleBuilder::SourceLine>& chlorineLines()
        {
            using Unit = AtomicScaleBuilder::WavelengthUnit;
            static const std::vector<AtomicScaleBuilder::SourceLine> lines {
                { "Cl-380.946", "Cl I 380.946", 380.9460, Unit::Nanometer, 0.01333333 },
                { "Cl-385.137", "Cl I 385.137", 385.1370, Unit::Nanometer, 0.01000000 },
                { "Cl-390.276", "Cl I 390.276", 390.2760, Unit::Nanometer, 0.00240000 },
                { "Cl-395.417", "Cl I 395.417", 395.4170, Unit::Nanometer, 0.00520000 },
                { "Cl-401.850", "Cl I 401.850", 401.8500, Unit::Nanometer, 0.26000000 },
                { "Cl-410.479", "Cl I 410.479", 410.4790, Unit::Nanometer, 0.00420000 },
                { "Cl-415.399", "Cl I 415.399", 415.3990, Unit::Nanometer, 0.00213333 },
                { "Cl-420.801", "Cl I 420.801", 420.8010, Unit::Nanometer, 0.13000000 },
                { "Cl-425.338", "Cl I 425.338", 425.3380, Unit::Nanometer, 0.35000000 },
                { "Cl-430.742", "Cl I 430.742", 430.7420, Unit::Nanometer, 0.09000000 },
                { "Cl-436.950", "Cl I 436.950", 436.9500, Unit::Nanometer, 0.02000000 },
                { "Cl-443.849", "Cl I 443.849", 443.8490, Unit::Nanometer, 0.42000000 },
                { "Cl-448.991", "Cl I 448.991", 448.9910, Unit::Nanometer, 0.38000000 },
                { "Cl-454.549", "Cl I 454.549", 454.5490, Unit::Nanometer, 0.01000000 },
                { "Cl-459.095", "Cl I 459.095", 459.0950, Unit::Nanometer, 0.00366667 },
                { "Cl-465.404", "Cl I 465.404", 465.4040, Unit::Nanometer, 0.03000000 },
                { "Cl-470.314", "Cl I 470.314", 470.3140, Unit::Nanometer, 0.49000000 },
                { "Cl-475.558", "Cl I 475.558", 475.5580, Unit::Nanometer, 0.11000000 },
                { "Cl-481.870", "Cl I 481.870", 481.8700, Unit::Nanometer, 0.00153333 },
                { "Cl-486.375", "Cl I 486.375", 486.3750, Unit::Nanometer, 0.19000000 },
                { "Cl-491.773", "Cl I 491.773", 491.7730, Unit::Nanometer, 0.24000000 },
                { "Cl-497.164", "Cl I 497.164", 497.1640, Unit::Nanometer, 0.03000000 },
                { "Cl-506.812", "Cl I 506.812", 506.8120, Unit::Nanometer, 0.01666667 },
                { "Cl-511.336", "Cl I 511.336", 511.3360, Unit::Nanometer, 0.16000000 },
                { "Cl-521.794", "Cl I 521.794", 521.7940, Unit::Nanometer, 0.49000000 },
                { "Cl-532.328", "Cl I 532.328", 532.3280, Unit::Nanometer, 0.00486667 },
                { "Cl-539.212", "Cl I 539.212", 539.2120, Unit::Nanometer, 0.03333333 },
                { "Cl-544.421", "Cl I 544.421", 544.4210, Unit::Nanometer, 0.13000000 },
                { "Cl-551.771", "Cl I 551.771", 551.7710, Unit::Nanometer, 0.00666667 },
                { "Cl-556.876", "Cl I 556.876", 556.8760, Unit::Nanometer, 0.07000000 },
                { "Cl-579.991", "Cl I 579.991", 579.9910, Unit::Nanometer, 0.12000000 },
                { "Cl-585.674", "Cl I 585.674", 585.6740, Unit::Nanometer, 0.04333333 },
                { "Cl-594.858", "Cl I 594.858", 594.8580, Unit::Nanometer, 0.31000000 },
                { "Cl-601.981", "Cl I 601.981", 601.9810, Unit::Nanometer, 0.14000000 },
                { "Cl-608.261", "Cl I 608.261", 608.2610, Unit::Nanometer, 0.09000000 },
                { "Cl-614.024", "Cl I 614.024", 614.0240, Unit::Nanometer, 0.08000000 },
                { "Cl-619.476", "Cl I 619.476", 619.4760, Unit::Nanometer, 0.07000000 },
                { "Cl-639.940", "Cl I 639.940", 639.9400, Unit::Nanometer, 0.13000000 },
                { "Cl-653.143", "Cl I 653.143", 653.1430, Unit::Nanometer, 0.11000000 },
                { "Cl-666.167", "Cl I 666.167", 666.1670, Unit::Nanometer, 0.39000000 },
                { "Cl-671.341", "Cl I 671.341", 671.3410, Unit::Nanometer, 0.32000000 },
                { "Cl-684.188", "Cl I 684.188", 684.1880, Unit::Nanometer, 0.17000000 },
                { "Cl-692.201", "Cl I 692.201", 692.2010, Unit::Nanometer, 0.04000000 },
                { "Cl-698.189", "Cl I 698.189", 698.1890, Unit::Nanometer, 0.17000000 },
            };
            return lines;
        }

        const std::vector<AtomicScaleBuilder::SourceLine>& argonLines()
        {
            using Unit = AtomicScaleBuilder::WavelengthUnit;
            static const std::vector<AtomicScaleBuilder::SourceLine> lines {
                { "Ar-380.857", "Ar I 380.857", 380.8570, Unit::Nanometer, 0.00197143 },
                { "Ar-385.058", "Ar I 385.058", 385.0580, Unit::Nanometer, 0.03000000 },
                { "Ar-392.862", "Ar I 392.862", 392.8620, Unit::Nanometer, 0.00285714 },
                { "Ar-401.386", "Ar I 401.386", 401.3860, Unit::Nanometer, 0.00285714 },
                { "Ar-407.238", "Ar I 407.238", 407.2380, Unit::Nanometer, 0.00108571 },
                { "Ar-413.172", "Ar I 413.172", 413.1720, Unit::Nanometer, 0.00262857 },
                { "Ar-418.188", "Ar I 418.188", 418.1880, Unit::Nanometer, 0.03000000 },
                { "Ar-423.722", "Ar I 423.722", 423.7220, Unit::Nanometer, 0.00148571 },
                { "Ar-428.290", "Ar I 428.290", 428.2900, Unit::Nanometer, 0.00182857 },
                { "Ar-433.356", "Ar I 433.356", 433.3560, Unit::Nanometer, 0.05142857 },
                { "Ar-440.099", "Ar I 440.099", 440.0990, Unit::Nanometer, 0.00148571 },
                { "Ar-448.181", "Ar I 448.181", 448.1810, Unit::Nanometer, 0.00254286 },
                { "Ar-454.505", "Ar I 454.505", 454.5050, Unit::Nanometer, 0.00714286 },
                { "Ar-459.610", "Ar I 459.610", 459.6100, Unit::Nanometer, 0.01285714 },
                { "Ar-464.214", "Ar I 464.214", 464.2140, Unit::Nanometer, 0.00131429 },
                { "Ar-470.232", "Ar I 470.232", 470.2320, Unit::Nanometer, 0.01285714 },
                { "Ar-475.294", "Ar I 475.294", 475.2940, Unit::Nanometer, 0.00191429 },
                { "Ar-480.602", "Ar I 480.602", 480.6020, Unit::Nanometer, 0.00714286 },
                { "Ar-487.986", "Ar I 487.986", 487.9860, Unit::Nanometer, 0.04285714 },
                { "Ar-492.103", "Ar I 492.103", 492.1030, Unit::Nanometer, 0.00214286 },
                { "Ar-498.994", "Ar I 498.994", 498.9940, Unit::Nanometer, 0.00105714 },
                { "Ar-504.881", "Ar I 504.881", 504.8810, Unit::Nanometer, 0.00428571 },
                { "Ar-511.820", "Ar I 511.820", 511.8200, Unit::Nanometer, 0.00122857 },
                { "Ar-516.229", "Ar I 516.229", 516.2290, Unit::Nanometer, 0.01142857 },
                { "Ar-521.477", "Ar I 521.477", 521.4770, Unit::Nanometer, 0.00205714 },
                { "Ar-530.951", "Ar I 530.951", 530.9510, Unit::Nanometer, 0.00182857 },
                { "Ar-537.349", "Ar I 537.349", 537.3490, Unit::Nanometer, 0.00177143 },
                { "Ar-542.135", "Ar I 542.135", 542.1350, Unit::Nanometer, 0.00285714 },
                { "Ar-547.345", "Ar I 547.345", 547.3450, Unit::Nanometer, 0.00257143 },
                { "Ar-552.496", "Ar I 552.496", 552.4960, Unit::Nanometer, 0.00165714 },
                { "Ar-557.254", "Ar I 557.254", 557.2540, Unit::Nanometer, 0.00428571 },
                { "Ar-565.913", "Ar I 565.913", 565.9130, Unit::Nanometer, 0.00222857 },
                { "Ar-573.952", "Ar I 573.952", 573.9520, Unit::Nanometer, 0.00248571 },
                { "Ar-583.426", "Ar I 583.426", 583.4260, Unit::Nanometer, 0.00285714 },
                { "Ar-588.858", "Ar I 588.858", 588.8580, Unit::Nanometer, 0.01285714 },
                { "Ar-594.267", "Ar I 594.267", 594.2670, Unit::Nanometer, 0.00171429 },
                { "Ar-599.900", "Ar I 599.900", 599.9000, Unit::Nanometer, 0.00428571 },
                { "Ar-604.447", "Ar I 604.447", 604.4470, Unit::Nanometer, 0.00197143 },
                { "Ar-609.880", "Ar I 609.880", 609.8800, Unit::Nanometer, 0.00122857 },
                { "Ar-614.544", "Ar I 614.544", 614.5440, Unit::Nanometer, 0.00222857 },
                { "Ar-621.594", "Ar I 621.594", 621.5940, Unit::Nanometer, 0.00234286 },
                { "Ar-629.687", "Ar I 629.687", 629.6870, Unit::Nanometer, 0.00171429 },
                { "Ar-636.958", "Ar I 636.958", 636.9580, Unit::Nanometer, 0.00131429 },
                { "Ar-641.631", "Ar I 641.631", 641.6310, Unit::Nanometer, 0.01000000 },
                { "Ar-648.308", "Ar I 648.308", 648.3080, Unit::Nanometer, 0.00131429 },
                { "Ar-653.805", "Ar I 653.805", 653.8050, Unit::Nanometer, 0.09000000 },
                { "Ar-659.868", "Ar I 659.868", 659.8680, Unit::Nanometer, 0.00194286 },
                { "Ar-664.370", "Ar I 664.370", 664.3700, Unit::Nanometer, 0.01285714 },
                { "Ar-669.887", "Ar I 669.887", 669.8870, Unit::Nanometer, 0.01428571 },
                { "Ar-675.616", "Ar I 675.616", 675.6160, Unit::Nanometer, 0.01857143 },
                { "Ar-680.853", "Ar I 680.853", 680.8530, Unit::Nanometer, 0.00111429 },
                { "Ar-685.188", "Ar I 685.188", 685.1880, Unit::Nanometer, 0.00128571 },
                { "Ar-693.766", "Ar I 693.766", 693.7660, Unit::Nanometer, 0.03857143 },
                { "Ar-698.571", "Ar I 698.571", 698.5710, Unit::Nanometer, 0.00228571 },
            };
            return lines;
        }

        const std::vector<AtomicScaleBuilder::SourceLine>& potassiumLines()
        {
            using Unit = AtomicScaleBuilder::WavelengthUnit;
            static const std::vector<AtomicScaleBuilder::SourceLine> lines {
                { "K-381.750", "K I 381.750", 381.7500, Unit::Nanometer, 0.01800000 },
                { "K-386.141", "K I 386.141", 386.1410, Unit::Nanometer, 0.00400000 },
                { "K-392.636", "K I 392.636", 392.6360, Unit::Nanometer, 0.00800000 },
                { "K-397.258", "K I 397.258", 397.2580, Unit::Nanometer, 0.01400000 },
                { "K-402.488", "K I 402.488", 402.4880, Unit::Nanometer, 0.01800000 },
                { "K-409.369", "K I 409.369", 409.3690, Unit::Nanometer, 0.02600000 },
                { "K-414.919", "K I 414.919", 414.9190, Unit::Nanometer, 0.01200000 },
                { "K-420.949", "K I 420.949", 420.9490, Unit::Nanometer, 0.01200000 },
                { "K-425.510", "K I 425.510", 425.5100, Unit::Nanometer, 0.00100000 },
                { "K-430.527", "K I 430.527", 430.5270, Unit::Nanometer, 0.00236000 },
                { "K-436.296", "K I 436.296", 436.2960, Unit::Nanometer, 0.02800000 },
                { "K-442.373", "K I 442.373", 442.3730, Unit::Nanometer, 0.01600000 },
                { "K-450.533", "K I 450.533", 450.5330, Unit::Nanometer, 0.06000000 },
                { "K-459.565", "K I 459.565", 459.5650, Unit::Nanometer, 0.07800000 },
                { "K-464.237", "K I 464.237", 464.2370, Unit::Nanometer, 0.09000000 },
                { "K-474.435", "K I 474.435", 474.4350, Unit::Nanometer, 0.01800000 },
                { "K-479.975", "K I 479.975", 479.9750, Unit::Nanometer, 0.00800000 },
                { "K-484.986", "K I 484.986", 484.9860, Unit::Nanometer, 0.00208000 },
                { "K-493.875", "K I 493.875", 493.8750, Unit::Nanometer, 0.00400000 },
                { "K-500.560", "K I 500.560", 500.5600, Unit::Nanometer, 0.00600000 },
                { "K-505.627", "K I 505.627", 505.6270, Unit::Nanometer, 0.02400000 },
                { "K-511.225", "K I 511.225", 511.2250, Unit::Nanometer, 0.01600000 },
                { "K-531.024", "K I 531.024", 531.0240, Unit::Nanometer, 0.01800000 },
                { "K-547.013", "K I 547.013", 547.0130, Unit::Nanometer, 0.03200000 },
                { "K-553.601", "K I 553.601", 553.6010, Unit::Nanometer, 0.01000000 },
                { "K-560.220", "K I 560.220", 560.2200, Unit::Nanometer, 0.00112000 },
                { "K-577.232", "K I 577.232", 577.2320, Unit::Nanometer, 0.01000000 },
                { "K-583.189", "K I 583.189", 583.1890, Unit::Nanometer, 0.03400000 },
                { "K-596.964", "K I 596.964", 596.9640, Unit::Nanometer, 0.01000000 },
                { "K-601.241", "K I 601.241", 601.2410, Unit::Nanometer, 0.00600000 },
                { "K-610.183", "K I 610.183", 610.1830, Unit::Nanometer, 0.00172000 },
                { "K-624.659", "K I 624.659", 624.6590, Unit::Nanometer, 0.02600000 },
                { "K-630.729", "K I 630.729", 630.7290, Unit::Nanometer, 0.03600000 },
                { "K-642.796", "K I 642.796", 642.7960, Unit::Nanometer, 0.01000000 },
                { "K-659.500", "K I 659.500", 659.5000, Unit::Nanometer, 0.00600000 },
                { "K-666.900", "K I 666.900", 666.9000, Unit::Nanometer, 0.00108000 },
                { "K-679.516", "K I 679.516", 679.5160, Unit::Nanometer, 0.00100000 },
                { "K-691.108", "K I 691.108", 691.1080, Unit::Nanometer, 0.04200000 },
                { "K-696.467", "K I 696.467", 696.4670, Unit::Nanometer, 0.00380000 },
            };
            return lines;
        }

        const std::vector<AtomicScaleBuilder::SourceLine>& calciumLines()
        {
            using Unit = AtomicScaleBuilder::WavelengthUnit;
            static const std::vector<AtomicScaleBuilder::SourceLine> lines {
                { "Ca-382.376", "Ca I 382.376", 382.3760, Unit::Nanometer, 0.01000000 },
                { "Ca-387.578", "Ca I 387.578", 387.5780, Unit::Nanometer, 0.00475000 },
                { "Ca-392.348", "Ca I 392.348", 392.3480, Unit::Nanometer, 0.00125000 },
                { "Ca-397.371", "Ca I 397.371", 397.3710, Unit::Nanometer, 0.30000000 },
                { "Ca-403.851", "Ca I 403.851", 403.8510, Unit::Nanometer, 0.01000000 },
                { "Ca-408.720", "Ca I 408.720", 408.7200, Unit::Nanometer, 0.00360000 },
                { "Ca-413.708", "Ca I 413.708", 413.7080, Unit::Nanometer, 0.02000000 },
                { "Ca-418.420", "Ca I 418.420", 418.4200, Unit::Nanometer, 0.03750000 },
                { "Ca-423.374", "Ca I 423.374", 423.3740, Unit::Nanometer, 0.00375000 },
                { "Ca-428.439", "Ca I 428.439", 428.4390, Unit::Nanometer, 0.02000000 },
                { "Ca-433.357", "Ca I 433.357", 433.3570, Unit::Nanometer, 0.15750000 },
                { "Ca-438.300", "Ca I 438.300", 438.3000, Unit::Nanometer, 0.01750000 },
                { "Ca-443.496", "Ca I 443.496", 443.4960, Unit::Nanometer, 0.48000000 },
                { "Ca-448.495", "Ca I 448.495", 448.4950, Unit::Nanometer, 0.01500000 },
                { "Ca-455.465", "Ca I 455.465", 455.4650, Unit::Nanometer, 0.00500000 },
                { "Ca-462.948", "Ca I 462.948", 462.9480, Unit::Nanometer, 0.01000000 },
                { "Ca-468.527", "Ca I 468.527", 468.5270, Unit::Nanometer, 0.07500000 },
                { "Ca-473.668", "Ca I 473.668", 473.6680, Unit::Nanometer, 0.01750000 },
                { "Ca-479.997", "Ca I 479.997", 479.9970, Unit::Nanometer, 0.01000000 },
                { "Ca-485.233", "Ca I 485.233", 485.2330, Unit::Nanometer, 0.00750000 },
                { "Ca-491.927", "Ca I 491.927", 491.9270, Unit::Nanometer, 0.01250000 },
                { "Ca-500.894", "Ca I 500.894", 500.8940, Unit::Nanometer, 0.01500000 },
                { "Ca-505.007", "Ca I 505.007", 505.0070, Unit::Nanometer, 0.01500000 },
                { "Ca-511.298", "Ca I 511.298", 511.2980, Unit::Nanometer, 0.01250000 },
                { "Ca-518.885", "Ca I 518.885", 518.8850, Unit::Nanometer, 0.18750000 },
                { "Ca-523.182", "Ca I 523.182", 523.1820, Unit::Nanometer, 0.02000000 },
                { "Ca-528.527", "Ca I 528.527", 528.5270, Unit::Nanometer, 0.02250000 },
                { "Ca-533.919", "Ca I 533.919", 533.9190, Unit::Nanometer, 0.04000000 },
                { "Ca-544.392", "Ca I 544.392", 544.3920, Unit::Nanometer, 0.00500000 },
                { "Ca-550.163", "Ca I 550.163", 550.1630, Unit::Nanometer, 0.02750000 },
                { "Ca-557.906", "Ca I 557.906", 557.9060, Unit::Nanometer, 0.01000000 },
                { "Ca-563.170", "Ca I 563.170", 563.1700, Unit::Nanometer, 0.00500000 },
                { "Ca-569.465", "Ca I 569.465", 569.4650, Unit::Nanometer, 0.01750000 },
                { "Ca-576.540", "Ca I 576.540", 576.5400, Unit::Nanometer, 0.00500000 },
                { "Ca-584.849", "Ca I 584.849", 584.8490, Unit::Nanometer, 0.00500000 },
                { "Ca-592.369", "Ca I 592.369", 592.3690, Unit::Nanometer, 0.00500000 },
                { "Ca-604.615", "Ca I 604.615", 604.6150, Unit::Nanometer, 0.02250000 },
                { "Ca-610.272", "Ca I 610.272", 610.2720, Unit::Nanometer, 0.39750000 },
                { "Ca-615.602", "Ca I 615.602", 615.6020, Unit::Nanometer, 0.01000000 },
                { "Ca-621.398", "Ca I 621.398", 621.3980, Unit::Nanometer, 0.01000000 },
                { "Ca-627.978", "Ca I 627.978", 627.9780, Unit::Nanometer, 0.01250000 },
                { "Ca-637.011", "Ca I 637.011", 637.0110, Unit::Nanometer, 0.02000000 },
                { "Ca-642.710", "Ca I 642.710", 642.7100, Unit::Nanometer, 0.00750000 },
                { "Ca-647.166", "Ca I 647.166", 647.1660, Unit::Nanometer, 0.34500000 },
                { "Ca-653.878", "Ca I 653.878", 653.8780, Unit::Nanometer, 0.02000000 },
                { "Ca-659.965", "Ca I 659.965", 659.9650, Unit::Nanometer, 0.01750000 },
                { "Ca-671.769", "Ca I 671.769", 671.7690, Unit::Nanometer, 0.03000000 },
                { "Ca-686.512", "Ca I 686.512", 686.5120, Unit::Nanometer, 0.33750000 },
                { "Ca-694.551", "Ca I 694.551", 694.5510, Unit::Nanometer, 0.36000000 },
            };
            return lines;
        }

        const std::vector<AtomicScaleBuilder::SourceLine>& scandiumLines()
        {
            using Unit = AtomicScaleBuilder::WavelengthUnit;
            static const std::vector<AtomicScaleBuilder::SourceLine> lines {
                { "Sc-380.853", "Sc I 380.853", 380.8530, Unit::Nanometer, 0.00100000 },
                { "Sc-385.810", "Sc I 385.810", 385.8100, Unit::Nanometer, 0.00900000 },
                { "Sc-390.643", "Sc I 390.643", 390.6430, Unit::Nanometer, 0.00400000 },
                { "Sc-395.701", "Sc I 395.701", 395.7010, Unit::Nanometer, 0.00450000 },
                { "Sc-400.842", "Sc I 400.842", 400.8420, Unit::Nanometer, 0.00400000 },
                { "Sc-405.260", "Sc I 405.260", 405.2600, Unit::Nanometer, 0.00600000 },
                { "Sc-410.469", "Sc I 410.469", 410.4690, Unit::Nanometer, 0.00150000 },
                { "Sc-415.574", "Sc I 415.574", 415.5740, Unit::Nanometer, 0.01200000 },
                { "Sc-420.678", "Sc I 420.678", 420.6780, Unit::Nanometer, 0.02250000 },
                { "Sc-425.729", "Sc I 425.729", 425.7290, Unit::Nanometer, 0.00300000 },
                { "Sc-430.930", "Sc I 430.930", 430.9300, Unit::Nanometer, 0.02400000 },
                { "Sc-435.685", "Sc I 435.685", 435.6850, Unit::Nanometer, 0.00550000 },
                { "Sc-440.443", "Sc I 440.443", 440.4430, Unit::Nanometer, 0.01500000 },
                { "Sc-449.621", "Sc I 449.621", 449.6210, Unit::Nanometer, 0.10000000 },
                { "Sc-454.298", "Sc I 454.298", 454.2980, Unit::Nanometer, 0.06600000 },
                { "Sc-459.469", "Sc I 459.469", 459.4690, Unit::Nanometer, 0.03150000 },
                { "Sc-464.208", "Sc I 464.208", 464.2080, Unit::Nanometer, 0.00650000 },
                { "Sc-469.828", "Sc I 469.828", 469.8280, Unit::Nanometer, 0.02850000 },
                { "Sc-474.382", "Sc I 474.382", 474.3820, Unit::Nanometer, 0.20000000 },
                { "Sc-479.292", "Sc I 479.292", 479.2920, Unit::Nanometer, 0.00800000 },
                { "Sc-484.086", "Sc I 484.086", 484.0860, Unit::Nanometer, 0.01000000 },
                { "Sc-489.298", "Sc I 489.298", 489.2980, Unit::Nanometer, 0.01950000 },
                { "Sc-494.131", "Sc I 494.131", 494.1310, Unit::Nanometer, 0.02850000 },
                { "Sc-499.289", "Sc I 499.289", 499.2890, Unit::Nanometer, 0.06900000 },
                { "Sc-504.445", "Sc I 504.445", 504.4450, Unit::Nanometer, 0.00700000 },
                { "Sc-509.672", "Sc I 509.672", 509.6720, Unit::Nanometer, 0.01500000 },
                { "Sc-514.707", "Sc I 514.707", 514.7070, Unit::Nanometer, 0.00800000 },
                { "Sc-519.379", "Sc I 519.379", 519.3790, Unit::Nanometer, 0.00800000 },
                { "Sc-525.836", "Sc I 525.836", 525.8360, Unit::Nanometer, 0.02250000 },
                { "Sc-530.195", "Sc I 530.195", 530.1950, Unit::Nanometer, 0.00400000 },
                { "Sc-535.610", "Sc I 535.610", 535.6100, Unit::Nanometer, 0.05700000 },
                { "Sc-540.408", "Sc I 540.408", 540.4080, Unit::Nanometer, 0.00800000 },
                { "Sc-545.229", "Sc I 545.229", 545.2290, Unit::Nanometer, 0.00250000 },
                { "Sc-550.174", "Sc I 550.174", 550.1740, Unit::Nanometer, 0.00900000 },
                { "Sc-555.360", "Sc I 555.360", 555.3600, Unit::Nanometer, 0.00600000 },
                { "Sc-560.892", "Sc I 560.892", 560.8920, Unit::Nanometer, 0.00300000 },
                { "Sc-565.836", "Sc I 565.836", 565.8360, Unit::Nanometer, 0.00900000 },
                { "Sc-570.682", "Sc I 570.682", 570.6820, Unit::Nanometer, 0.01950000 },
                { "Sc-577.163", "Sc I 577.163", 577.1630, Unit::Nanometer, 0.02850000 },
                { "Sc-582.353", "Sc I 582.353", 582.3530, Unit::Nanometer, 0.04050000 },
                { "Sc-589.456", "Sc I 589.456", 589.4560, Unit::Nanometer, 0.04950000 },
                { "Sc-594.265", "Sc I 594.265", 594.2650, Unit::Nanometer, 0.01500000 },
                { "Sc-599.285", "Sc I 599.285", 599.2850, Unit::Nanometer, 0.02850000 },
                { "Sc-604.889", "Sc I 604.889", 604.8890, Unit::Nanometer, 0.06150000 },
                { "Sc-609.589", "Sc I 609.589", 609.5890, Unit::Nanometer, 0.10000000 },
                { "Sc-614.620", "Sc I 614.620", 614.6200, Unit::Nanometer, 0.08700000 },
                { "Sc-619.843", "Sc I 619.843", 619.8430, Unit::Nanometer, 0.15000000 },
                { "Sc-624.564", "Sc I 624.564", 624.5640, Unit::Nanometer, 0.04500000 },
                { "Sc-629.764", "Sc I 629.764", 629.7640, Unit::Nanometer, 0.00450000 },
                { "Sc-634.482", "Sc I 634.482", 634.4820, Unit::Nanometer, 0.01350000 },
                { "Sc-639.854", "Sc I 639.854", 639.8540, Unit::Nanometer, 0.02550000 },
                { "Sc-644.672", "Sc I 644.672", 644.6720, Unit::Nanometer, 0.02250000 },
                { "Sc-649.651", "Sc I 649.651", 649.6510, Unit::Nanometer, 0.08100000 },
                { "Sc-654.788", "Sc I 654.788", 654.7880, Unit::Nanometer, 0.02250000 },
                { "Sc-659.721", "Sc I 659.721", 659.7210, Unit::Nanometer, 0.06150000 },
                { "Sc-664.622", "Sc I 664.622", 664.6220, Unit::Nanometer, 0.01800000 },
                { "Sc-669.922", "Sc I 669.922", 669.9220, Unit::Nanometer, 0.08700000 },
                { "Sc-675.113", "Sc I 675.113", 675.1130, Unit::Nanometer, 0.07950000 },
                { "Sc-680.461", "Sc I 680.461", 680.4610, Unit::Nanometer, 0.10000000 },
                { "Sc-685.653", "Sc I 685.653", 685.6530, Unit::Nanometer, 0.02850000 },
                { "Sc-694.912", "Sc I 694.912", 694.9120, Unit::Nanometer, 0.06000000 },
            };
            return lines;
        }

        const std::vector<AtomicScaleBuilder::SourceLine>& titaniumLines()
        {
            using Unit = AtomicScaleBuilder::WavelengthUnit;
            static const std::vector<AtomicScaleBuilder::SourceLine> lines {
                { "Ti-380.604", "Ti I 380.604", 380.6040, Unit::Nanometer, 0.00250000 },
                { "Ti-385.790", "Ti I 385.790", 385.7900, Unit::Nanometer, 0.00700000 },
                { "Ti-390.096", "Ti I 390.096", 390.0960, Unit::Nanometer, 0.02850000 },
                { "Ti-395.820", "Ti I 395.820", 395.8200, Unit::Nanometer, 0.15000000 },
                { "Ti-400.646", "Ti I 400.646", 400.6460, Unit::Nanometer, 0.00150000 },
                { "Ti-405.619", "Ti I 405.619", 405.6190, Unit::Nanometer, 0.00150000 },
                { "Ti-410.005", "Ti I 410.005", 410.0050, Unit::Nanometer, 0.00150000 },
                { "Ti-415.485", "Ti I 415.485", 415.4850, Unit::Nanometer, 0.00600000 },
                { "Ti-420.341", "Ti I 420.341", 420.3410, Unit::Nanometer, 0.00300000 },
                { "Ti-425.411", "Ti I 425.411", 425.4110, Unit::Nanometer, 0.00200000 },
                { "Ti-430.451", "Ti I 430.451", 430.4510, Unit::Nanometer, 0.00200000 },
                { "Ti-435.528", "Ti I 435.528", 435.5280, Unit::Nanometer, 0.01050000 },
                { "Ti-440.568", "Ti I 440.568", 440.5680, Unit::Nanometer, 0.02850000 },
                { "Ti-445.370", "Ti I 445.370", 445.3700, Unit::Nanometer, 0.10000000 },
                { "Ti-450.571", "Ti I 450.571", 450.5710, Unit::Nanometer, 0.00750000 },
                { "Ti-455.548", "Ti I 455.548", 455.5480, Unit::Nanometer, 0.25000000 },
                { "Ti-460.927", "Ti I 460.927", 460.9270, Unit::Nanometer, 0.02100000 },
                { "Ti-465.578", "Ti I 465.578", 465.5780, Unit::Nanometer, 0.00200000 },
                { "Ti-470.866", "Ti I 470.866", 470.8660, Unit::Nanometer, 0.01200000 },
                { "Ti-475.890", "Ti I 475.890", 475.8900, Unit::Nanometer, 0.00500000 },
                { "Ti-480.542", "Ti I 480.542", 480.5420, Unit::Nanometer, 0.07800000 },
                { "Ti-485.591", "Ti I 485.591", 485.5910, Unit::Nanometer, 0.00550000 },
                { "Ti-490.374", "Ti I 490.374", 490.3740, Unit::Nanometer, 0.00850000 },
                { "Ti-495.828", "Ti I 495.828", 495.8280, Unit::Nanometer, 0.01350000 },
                { "Ti-500.517", "Ti I 500.517", 500.5170, Unit::Nanometer, 0.00350000 },
                { "Ti-505.408", "Ti I 505.408", 505.4080, Unit::Nanometer, 0.00550000 },
                { "Ti-510.943", "Ti I 510.943", 510.9430, Unit::Nanometer, 0.02250000 },
                { "Ti-515.407", "Ti I 515.407", 515.4070, Unit::Nanometer, 0.00850000 },
                { "Ti-520.609", "Ti I 520.609", 520.6090, Unit::Nanometer, 0.04650000 },
                { "Ti-525.581", "Ti I 525.581", 525.5810, Unit::Nanometer, 0.06000000 },
                { "Ti-530.120", "Ti I 530.120", 530.1200, Unit::Nanometer, 0.00200000 },
                { "Ti-535.651", "Ti I 535.651", 535.6510, Unit::Nanometer, 0.00250000 },
                { "Ti-540.894", "Ti I 540.894", 540.8940, Unit::Nanometer, 0.00500000 },
                { "Ti-545.407", "Ti I 545.407", 545.4070, Unit::Nanometer, 0.00250000 },
                { "Ti-550.390", "Ti I 550.390", 550.3900, Unit::Nanometer, 0.04050000 },
                { "Ti-556.658", "Ti I 556.658", 556.6580, Unit::Nanometer, 0.00150000 },
                { "Ti-561.835", "Ti I 561.835", 561.8350, Unit::Nanometer, 0.00150000 },
                { "Ti-566.288", "Ti I 566.288", 566.2880, Unit::Nanometer, 0.01350000 },
                { "Ti-571.511", "Ti I 571.511", 571.5110, Unit::Nanometer, 0.02250000 },
                { "Ti-576.355", "Ti I 576.355", 576.3550, Unit::Nanometer, 0.00100000 },
                { "Ti-581.395", "Ti I 581.395", 581.3950, Unit::Nanometer, 0.00300000 },
                { "Ti-586.636", "Ti I 586.636", 586.6360, Unit::Nanometer, 0.00500000 },
                { "Ti-591.409", "Ti I 591.409", 591.4090, Unit::Nanometer, 0.00400000 },
                { "Ti-596.583", "Ti I 596.583", 596.5830, Unit::Nanometer, 0.04500000 },
                { "Ti-601.477", "Ti I 601.477", 601.4770, Unit::Nanometer, 0.00200000 },
                { "Ti-606.535", "Ti I 606.535", 606.5350, Unit::Nanometer, 0.02550000 },
                { "Ti-612.379", "Ti I 612.379", 612.3790, Unit::Nanometer, 0.00300000 },
                { "Ti-617.475", "Ti I 617.475", 617.4750, Unit::Nanometer, 0.01500000 },
                { "Ti-622.130", "Ti I 622.130", 622.1300, Unit::Nanometer, 0.01800000 },
                { "Ti-629.594", "Ti I 629.594", 629.5940, Unit::Nanometer, 0.00150000 },
                { "Ti-635.917", "Ti I 635.917", 635.9170, Unit::Nanometer, 0.00150000 },
                { "Ti-640.780", "Ti I 640.780", 640.7800, Unit::Nanometer, 0.00300000 },
                { "Ti-647.944", "Ti I 647.944", 647.9440, Unit::Nanometer, 0.00150000 },
                { "Ti-652.658", "Ti I 652.658", 652.6580, Unit::Nanometer, 0.00100000 },
                { "Ti-657.516", "Ti I 657.516", 657.5160, Unit::Nanometer, 0.00700000 },
                { "Ti-662.838", "Ti I 662.838", 662.8380, Unit::Nanometer, 0.00100000 },
                { "Ti-667.718", "Ti I 667.718", 667.7180, Unit::Nanometer, 0.01950000 },
                { "Ti-672.480", "Ti I 672.480", 672.4800, Unit::Nanometer, 0.00250000 },
                { "Ti-677.622", "Ti I 677.622", 677.6220, Unit::Nanometer, 0.00450000 },
                { "Ti-682.795", "Ti I 682.795", 682.7950, Unit::Nanometer, 0.00250000 },
                { "Ti-687.389", "Ti I 687.389", 687.3890, Unit::Nanometer, 0.00800000 },
                { "Ti-692.620", "Ti I 692.620", 692.6200, Unit::Nanometer, 0.03150000 },
                { "Ti-697.699", "Ti I 697.699", 697.6990, Unit::Nanometer, 0.00550000 },
            };
            return lines;
        }

        const std::vector<AtomicScaleBuilder::SourceLine>& vanadiumLines()
        {
            using Unit = AtomicScaleBuilder::WavelengthUnit;
            static const std::vector<AtomicScaleBuilder::SourceLine> lines {
                { "V-380.680", "V I 380.680", 380.6800, Unit::Nanometer, 0.14814815 },
                { "V-385.584", "V I 385.584", 385.5840, Unit::Nanometer, 0.64814815 },
                { "V-390.446", "V I 390.446", 390.4460, Unit::Nanometer, 0.03086420 },
                { "V-395.023", "V I 395.023", 395.0230, Unit::Nanometer, 0.01851852 },
                { "V-400.352", "V I 400.352", 400.3520, Unit::Nanometer, 0.01012346 },
                { "V-405.365", "V I 405.365", 405.3650, Unit::Nanometer, 0.00222222 },
                { "V-410.477", "V I 410.477", 410.4770, Unit::Nanometer, 0.01111111 },
                { "V-415.589", "V I 415.589", 415.5890, Unit::Nanometer, 0.00839506 },
                { "V-420.420", "V I 420.420", 420.4200, Unit::Nanometer, 0.01234568 },
                { "V-425.736", "V I 425.736", 425.7360, Unit::Nanometer, 0.03086420 },
                { "V-430.656", "V I 430.656", 430.6560, Unit::Nanometer, 0.00641975 },
                { "V-435.497", "V I 435.497", 435.4970, Unit::Nanometer, 0.04320988 },
                { "V-440.610", "V I 440.610", 440.6100, Unit::Nanometer, 0.12345679 },
                { "V-445.412", "V I 445.412", 445.4120, Unit::Nanometer, 0.06172840 },
                { "V-450.208", "V I 450.208", 450.2080, Unit::Nanometer, 0.01851852 },
                { "V-455.536", "V I 455.536", 455.5360, Unit::Nanometer, 0.03086420 },
                { "V-460.615", "V I 460.615", 460.6150, Unit::Nanometer, 0.20370370 },
                { "V-465.655", "V I 465.655", 465.6550, Unit::Nanometer, 0.02469136 },
                { "V-470.656", "V I 470.656", 470.6560, Unit::Nanometer, 0.11111111 },
                { "V-475.638", "V I 475.638", 475.6380, Unit::Nanometer, 0.01049383 },
                { "V-480.752", "V I 480.752", 480.7520, Unit::Nanometer, 0.31481481 },
                { "V-485.655", "V I 485.655", 485.6550, Unit::Nanometer, 0.00765432 },
                { "V-490.443", "V I 490.443", 490.4430, Unit::Nanometer, 0.03703704 },
                { "V-495.813", "V I 495.813", 495.8130, Unit::Nanometer, 0.00333333 },
                { "V-500.562", "V I 500.562", 500.5620, Unit::Nanometer, 0.02469136 },
                { "V-505.394", "V I 505.394", 505.3940, Unit::Nanometer, 0.01234568 },
                { "V-510.622", "V I 510.622", 510.6220, Unit::Nanometer, 0.00469136 },
                { "V-515.701", "V I 515.701", 515.7010, Unit::Nanometer, 0.04320988 },
                { "V-520.658", "V I 520.658", 520.6580, Unit::Nanometer, 0.04320988 },
                { "V-525.814", "V I 525.814", 525.8140, Unit::Nanometer, 0.01851852 },
                { "V-530.215", "V I 530.215", 530.2150, Unit::Nanometer, 0.02469136 },
                { "V-535.341", "V I 535.341", 535.3410, Unit::Nanometer, 0.25925926 },
                { "V-540.449", "V I 540.449", 540.4490, Unit::Nanometer, 0.00222222 },
                { "V-545.709", "V I 545.709", 545.7090, Unit::Nanometer, 0.00864198 },
                { "V-550.663", "V I 550.663", 550.6630, Unit::Nanometer, 0.00506173 },
                { "V-555.745", "V I 555.745", 555.7450, Unit::Nanometer, 0.07407407 },
                { "V-560.463", "V I 560.463", 560.4630, Unit::Nanometer, 0.03086420 },
                { "V-565.629", "V I 565.629", 565.6290, Unit::Nanometer, 0.01135802 },
                { "V-570.779", "V I 570.779", 570.7790, Unit::Nanometer, 0.00370370 },
                { "V-575.685", "V I 575.685", 575.6850, Unit::Nanometer, 0.00172840 },
                { "V-580.710", "V I 580.710", 580.7100, Unit::Nanometer, 0.08024691 },
                { "V-585.617", "V I 585.617", 585.6170, Unit::Nanometer, 0.00160494 },
                { "V-591.634", "V I 591.634", 591.6340, Unit::Nanometer, 0.03703704 },
                { "V-596.548", "V I 596.548", 596.5480, Unit::Nanometer, 0.07407407 },
                { "V-601.790", "V I 601.790", 601.7900, Unit::Nanometer, 0.02469136 },
                { "V-606.725", "V I 606.725", 606.7250, Unit::Nanometer, 0.05555556 },
                { "V-611.302", "V I 611.302", 611.3020, Unit::Nanometer, 0.02469136 },
                { "V-616.682", "V I 616.682", 616.6820, Unit::Nanometer, 0.02469136 },
                { "V-621.636", "V I 621.636", 621.6360, Unit::Nanometer, 0.55555556 },
                { "V-626.632", "V I 626.632", 626.6320, Unit::Nanometer, 0.14814815 },
                { "V-631.148", "V I 631.148", 631.1480, Unit::Nanometer, 0.14814815 },
                { "V-636.216", "V I 636.216", 636.2160, Unit::Nanometer, 0.00604938 },
                { "V-641.704", "V I 641.704", 641.7040, Unit::Nanometer, 0.01074074 },
                { "V-646.699", "V I 646.699", 646.6990, Unit::Nanometer, 0.01851852 },
                { "V-651.672", "V I 651.672", 651.6720, Unit::Nanometer, 0.00172840 },
                { "V-656.588", "V I 656.588", 656.5880, Unit::Nanometer, 0.01148148 },
                { "V-661.371", "V I 661.371", 661.3710, Unit::Nanometer, 0.00370370 },
                { "V-666.239", "V I 666.239", 666.2390, Unit::Nanometer, 0.03703704 },
                { "V-671.876", "V I 671.876", 671.8760, Unit::Nanometer, 0.00160494 },
                { "V-676.651", "V I 676.651", 676.6510, Unit::Nanometer, 0.08641975 },
                { "V-681.240", "V I 681.240", 681.2400, Unit::Nanometer, 0.11111111 },
                { "V-687.152", "V I 687.152", 687.1520, Unit::Nanometer, 0.00765432 },
                { "V-693.702", "V I 693.702", 693.7020, Unit::Nanometer, 0.00691358 },
                { "V-699.240", "V I 699.240", 699.2400, Unit::Nanometer, 0.00197531 },
            };
            return lines;
        }

        const std::vector<AtomicScaleBuilder::SourceLine>& chromiumLines()
        {
            using Unit = AtomicScaleBuilder::WavelengthUnit;
            static const std::vector<AtomicScaleBuilder::SourceLine> lines {
                { "Cr-380.683", "Cr I 380.683", 380.6830, Unit::Nanometer, 0.04750000 },
                { "Cr-385.422", "Cr I 385.422", 385.4220, Unit::Nanometer, 0.04250000 },
                { "Cr-390.645", "Cr I 390.645", 390.6450, Unit::Nanometer, 0.00240000 },
                { "Cr-395.383", "Cr I 395.383", 395.3830, Unit::Nanometer, 0.00135000 },
                { "Cr-400.391", "Cr I 400.391", 400.3910, Unit::Nanometer, 0.01250000 },
                { "Cr-405.678", "Cr I 405.678", 405.6780, Unit::Nanometer, 0.00750000 },
                { "Cr-410.486", "Cr I 410.486", 410.4860, Unit::Nanometer, 0.01750000 },
                { "Cr-415.306", "Cr I 415.306", 415.3060, Unit::Nanometer, 0.02000000 },
                { "Cr-420.690", "Cr I 420.690", 420.6900, Unit::Nanometer, 0.06000000 },
                { "Cr-425.498", "Cr I 425.498", 425.4980, Unit::Nanometer, 0.01000000 },
                { "Cr-430.474", "Cr I 430.474", 430.4740, Unit::Nanometer, 0.00200000 },
                { "Cr-435.676", "Cr I 435.676", 435.6760, Unit::Nanometer, 0.02000000 },
                { "Cr-440.675", "Cr I 440.675", 440.6750, Unit::Nanometer, 0.00750000 },
                { "Cr-445.636", "Cr I 445.636", 445.6360, Unit::Nanometer, 0.00330000 },
                { "Cr-450.522", "Cr I 450.522", 450.5220, Unit::Nanometer, 0.00145000 },
                { "Cr-455.529", "Cr I 455.529", 455.5290, Unit::Nanometer, 0.02000000 },
                { "Cr-460.384", "Cr I 460.384", 460.3840, Unit::Nanometer, 0.00290000 },
                { "Cr-465.472", "Cr I 465.472", 465.4720, Unit::Nanometer, 0.18750000 },
                { "Cr-470.567", "Cr I 470.567", 470.5670, Unit::Nanometer, 0.00225000 },
                { "Cr-475.612", "Cr I 475.612", 475.6120, Unit::Nanometer, 0.42750000 },
                { "Cr-480.462", "Cr I 480.462", 480.4620, Unit::Nanometer, 0.05250000 },
                { "Cr-485.619", "Cr I 485.619", 485.6190, Unit::Nanometer, 0.09000000 },
                { "Cr-490.505", "Cr I 490.505", 490.5050, Unit::Nanometer, 0.02250000 },
                { "Cr-495.481", "Cr I 495.481", 495.4810, Unit::Nanometer, 0.16500000 },
                { "Cr-500.508", "Cr I 500.508", 500.5080, Unit::Nanometer, 0.00455000 },
                { "Cr-505.513", "Cr I 505.513", 505.5130, Unit::Nanometer, 0.00500000 },
                { "Cr-510.891", "Cr I 510.891", 510.8910, Unit::Nanometer, 0.02000000 },
                { "Cr-515.191", "Cr I 515.191", 515.1910, Unit::Nanometer, 0.00750000 },
                { "Cr-520.634", "Cr I 520.634", 520.6340, Unit::Nanometer, 0.00150000 },
                { "Cr-525.512", "Cr I 525.512", 525.5120, Unit::Nanometer, 0.03000000 },
                { "Cr-530.728", "Cr I 530.728", 530.7280, Unit::Nanometer, 0.01750000 },
                { "Cr-535.651", "Cr I 535.651", 535.6510, Unit::Nanometer, 0.01500000 },
                { "Cr-540.643", "Cr I 540.643", 540.6430, Unit::Nanometer, 0.01000000 },
                { "Cr-546.396", "Cr I 546.396", 546.3960, Unit::Nanometer, 0.05250000 },
                { "Cr-551.270", "Cr I 551.270", 551.2700, Unit::Nanometer, 0.01500000 },
                { "Cr-556.655", "Cr I 556.655", 556.6550, Unit::Nanometer, 0.01000000 },
                { "Cr-561.710", "Cr I 561.710", 561.7100, Unit::Nanometer, 0.00470000 },
                { "Cr-566.458", "Cr I 566.458", 566.4580, Unit::Nanometer, 0.00750000 },
                { "Cr-571.520", "Cr I 571.520", 571.5200, Unit::Nanometer, 0.02500000 },
                { "Cr-576.213", "Cr I 576.213", 576.2130, Unit::Nanometer, 0.01250000 },
                { "Cr-581.849", "Cr I 581.849", 581.8490, Unit::Nanometer, 0.01000000 },
                { "Cr-586.444", "Cr I 586.444", 586.4440, Unit::Nanometer, 0.02250000 },
                { "Cr-591.676", "Cr I 591.676", 591.6760, Unit::Nanometer, 0.00500000 },
                { "Cr-596.458", "Cr I 596.458", 596.4580, Unit::Nanometer, 0.00500000 },
                { "Cr-601.479", "Cr I 601.479", 601.4790, Unit::Nanometer, 0.03250000 },
                { "Cr-606.847", "Cr I 606.847", 606.8470, Unit::Nanometer, 0.05250000 },
                { "Cr-612.249", "Cr I 612.249", 612.2490, Unit::Nanometer, 0.02750000 },
                { "Cr-617.910", "Cr I 617.910", 617.9100, Unit::Nanometer, 0.00405000 },
                { "Cr-622.491", "Cr I 622.491", 622.4910, Unit::Nanometer, 0.00330000 },
                { "Cr-628.562", "Cr I 628.562", 628.5620, Unit::Nanometer, 0.07500000 },
                { "Cr-633.328", "Cr I 633.328", 633.3280, Unit::Nanometer, 0.00195000 },
                { "Cr-638.314", "Cr I 638.314", 638.3140, Unit::Nanometer, 0.00290000 },
                { "Cr-643.620", "Cr I 643.620", 643.6200, Unit::Nanometer, 0.00185000 },
                { "Cr-649.168", "Cr I 649.168", 649.1680, Unit::Nanometer, 0.02500000 },
                { "Cr-654.702", "Cr I 654.702", 654.7020, Unit::Nanometer, 0.01000000 },
                { "Cr-659.232", "Cr I 659.232", 659.2320, Unit::Nanometer, 0.07500000 },
                { "Cr-664.578", "Cr I 664.578", 664.5780, Unit::Nanometer, 0.01000000 },
                { "Cr-669.840", "Cr I 669.840", 669.8400, Unit::Nanometer, 0.00750000 },
                { "Cr-674.750", "Cr I 674.750", 674.7500, Unit::Nanometer, 0.00405000 },
                { "Cr-679.806", "Cr I 679.806", 679.8060, Unit::Nanometer, 0.01750000 },
                { "Cr-684.500", "Cr I 684.500", 684.5000, Unit::Nanometer, 0.07500000 },
                { "Cr-689.600", "Cr I 689.600", 689.6000, Unit::Nanometer, 0.00375000 },
                { "Cr-697.979", "Cr I 697.979", 697.9790, Unit::Nanometer, 0.02250000 },
            };
            return lines;
        }

        const std::vector<AtomicScaleBuilder::SourceLine>& manganeseLines()
        {
            using Unit = AtomicScaleBuilder::WavelengthUnit;
            static const std::vector<AtomicScaleBuilder::SourceLine> lines {
                { "Mn-380.389", "Mn I 380.389", 380.3890, Unit::Nanometer, 0.00100000 },
                { "Mn-385.791", "Mn I 385.791", 385.7910, Unit::Nanometer, 0.00184444 },
                { "Mn-390.547", "Mn I 390.547", 390.5470, Unit::Nanometer, 0.02000000 },
                { "Mn-395.284", "Mn I 395.284", 395.2840, Unit::Nanometer, 0.07333333 },
                { "Mn-400.326", "Mn I 400.326", 400.3260, Unit::Nanometer, 0.01111111 },
                { "Mn-405.554", "Mn I 405.554", 405.5540, Unit::Nanometer, 0.04666667 },
                { "Mn-410.536", "Mn I 410.536", 410.5360, Unit::Nanometer, 0.05000000 },
                { "Mn-415.557", "Mn I 415.557", 415.5570, Unit::Nanometer, 0.04333333 },
                { "Mn-420.537", "Mn I 420.537", 420.5370, Unit::Nanometer, 0.05666667 },
                { "Mn-425.766", "Mn I 425.766", 425.7660, Unit::Nanometer, 0.12333333 },
                { "Mn-430.296", "Mn I 430.296", 430.2960, Unit::Nanometer, 0.04666667 },
                { "Mn-435.963", "Mn I 435.963", 435.9630, Unit::Nanometer, 0.01333333 },
                { "Mn-440.809", "Mn I 440.809", 440.8090, Unit::Nanometer, 0.14666667 },
                { "Mn-445.582", "Mn I 445.582", 445.5820, Unit::Nanometer, 0.20666667 },
                { "Mn-450.387", "Mn I 450.387", 450.3870, Unit::Nanometer, 0.33333333 },
                { "Mn-456.470", "Mn I 456.470", 456.4700, Unit::Nanometer, 0.00168889 },
                { "Mn-461.059", "Mn I 461.059", 461.0590, Unit::Nanometer, 0.00222222 },
                { "Mn-467.169", "Mn I 467.169", 467.1690, Unit::Nanometer, 0.10666667 },
                { "Mn-472.786", "Mn I 472.786", 472.7860, Unit::Nanometer, 0.03666667 },
                { "Mn-478.342", "Mn I 478.342", 478.3420, Unit::Nanometer, 1.00000000 },
                { "Mn-483.977", "Mn I 483.977", 483.9770, Unit::Nanometer, 0.01222222 },
                { "Mn-490.100", "Mn I 490.100", 490.1000, Unit::Nanometer, 0.00777778 },
                { "Mn-496.588", "Mn I 496.588", 496.5880, Unit::Nanometer, 0.07333333 },
                { "Mn-502.980", "Mn I 502.980", 502.9800, Unit::Nanometer, 0.09000000 },
                { "Mn-507.920", "Mn I 507.920", 507.9200, Unit::Nanometer, 0.10000000 },
                { "Mn-512.332", "Mn I 512.332", 512.3320, Unit::Nanometer, 0.01000000 },
                { "Mn-517.765", "Mn I 517.765", 517.7650, Unit::Nanometer, 0.00666667 },
                { "Mn-525.223", "Mn I 525.223", 525.2230, Unit::Nanometer, 0.09333333 },
                { "Mn-530.735", "Mn I 530.735", 530.7350, Unit::Nanometer, 0.01000000 },
                { "Mn-536.680", "Mn I 536.680", 536.6800, Unit::Nanometer, 0.00177778 },
                { "Mn-541.483", "Mn I 541.483", 541.4830, Unit::Nanometer, 0.03000000 },
                { "Mn-547.346", "Mn I 547.346", 547.3460, Unit::Nanometer, 0.00333333 },
                { "Mn-553.776", "Mn I 553.776", 553.7760, Unit::Nanometer, 0.09333333 },
                { "Mn-559.070", "Mn I 559.070", 559.0700, Unit::Nanometer, 0.00444444 },
                { "Mn-567.112", "Mn I 567.112", 567.1120, Unit::Nanometer, 0.03666667 },
                { "Mn-572.857", "Mn I 572.857", 572.8570, Unit::Nanometer, 0.00222222 },
                { "Mn-577.510", "Mn I 577.510", 577.5100, Unit::Nanometer, 0.00222222 },
                { "Mn-582.629", "Mn I 582.629", 582.6290, Unit::Nanometer, 0.07333333 },
                { "Mn-588.540", "Mn I 588.540", 588.5400, Unit::Nanometer, 0.00333333 },
                { "Mn-594.665", "Mn I 594.665", 594.6650, Unit::Nanometer, 0.06000000 },
                { "Mn-599.010", "Mn I 599.010", 599.0100, Unit::Nanometer, 0.00193333 },
                { "Mn-604.536", "Mn I 604.536", 604.5360, Unit::Nanometer, 0.00444444 },
                { "Mn-610.721", "Mn I 610.721", 610.7210, Unit::Nanometer, 0.00333333 },
                { "Mn-619.097", "Mn I 619.097", 619.0970, Unit::Nanometer, 0.00146667 },
                { "Mn-624.094", "Mn I 624.094", 624.0940, Unit::Nanometer, 0.00144444 },
                { "Mn-633.000", "Mn I 633.000", 633.0000, Unit::Nanometer, 0.00333333 },
                { "Mn-638.467", "Mn I 638.467", 638.4670, Unit::Nanometer, 0.03333333 },
                { "Mn-643.176", "Mn I 643.176", 643.1760, Unit::Nanometer, 0.00157778 },
                { "Mn-649.704", "Mn I 649.704", 649.7040, Unit::Nanometer, 0.04000000 },
                { "Mn-654.517", "Mn I 654.517", 654.5170, Unit::Nanometer, 0.00777778 },
                { "Mn-660.926", "Mn I 660.926", 660.9260, Unit::Nanometer, 0.04666667 },
                { "Mn-665.700", "Mn I 665.700", 665.7000, Unit::Nanometer, 0.01333333 },
                { "Mn-670.902", "Mn I 670.902", 670.9020, Unit::Nanometer, 0.00444444 },
                { "Mn-676.348", "Mn I 676.348", 676.3480, Unit::Nanometer, 0.00222222 },
                { "Mn-685.210", "Mn I 685.210", 685.2100, Unit::Nanometer, 0.00333333 },
                { "Mn-690.763", "Mn I 690.763", 690.7630, Unit::Nanometer, 0.00777778 },
                { "Mn-696.530", "Mn I 696.530", 696.5300, Unit::Nanometer, 0.01000000 },
            };
            return lines;
        }

        const std::vector<AtomicScaleBuilder::SourceLine>& ironLines()
        {
            using Unit = AtomicScaleBuilder::WavelengthUnit;
            static const std::vector<AtomicScaleBuilder::SourceLine> lines {
                { "Fe-380.622", "Fe I 380.622", 380.6220, Unit::Nanometer, 0.03333333 },
                { "Fe-385.531", "Fe I 385.531", 385.5310, Unit::Nanometer, 0.00113333 },
                { "Fe-390.675", "Fe I 390.675", 390.6750, Unit::Nanometer, 0.00380000 },
                { "Fe-395.433", "Fe I 395.433", 395.4330, Unit::Nanometer, 0.00580000 },
                { "Fe-400.631", "Fe I 400.631", 400.6310, Unit::Nanometer, 0.02000000 },
                { "Fe-405.481", "Fe I 405.481", 405.4810, Unit::Nanometer, 0.01333333 },
                { "Fe-410.494", "Fe I 410.494", 410.4940, Unit::Nanometer, 0.00266667 },
                { "Fe-415.481", "Fe I 415.481", 415.4810, Unit::Nanometer, 0.05666667 },
                { "Fe-420.398", "Fe I 420.398", 420.3980, Unit::Nanometer, 0.15000000 },
                { "Fe-425.620", "Fe I 425.620", 425.6200, Unit::Nanometer, 0.00300000 },
                { "Fe-430.545", "Fe I 430.545", 430.5450, Unit::Nanometer, 0.06333333 },
                { "Fe-435.273", "Fe I 435.273", 435.2730, Unit::Nanometer, 0.25000000 },
                { "Fe-440.475", "Fe I 440.475", 440.4750, Unit::Nanometer, 0.66666667 },
                { "Fe-445.503", "Fe I 445.503", 445.5030, Unit::Nanometer, 0.01333333 },
                { "Fe-450.828", "Fe I 450.828", 450.8280, Unit::Nanometer, 0.04666667 },
                { "Fe-455.589", "Fe I 455.589", 455.5890, Unit::Nanometer, 0.01000000 },
                { "Fe-460.713", "Fe I 460.713", 460.7130, Unit::Nanometer, 0.00100000 },
                { "Fe-465.460", "Fe I 465.460", 465.4600, Unit::Nanometer, 0.00140000 },
                { "Fe-470.727", "Fe I 470.727", 470.7270, Unit::Nanometer, 0.08000000 },
                { "Fe-475.758", "Fe I 475.758", 475.7580, Unit::Nanometer, 0.00240000 },
                { "Fe-480.288", "Fe I 480.288", 480.2880, Unit::Nanometer, 0.00166667 },
                { "Fe-485.974", "Fe I 485.974", 485.9740, Unit::Nanometer, 0.27000000 },
                { "Fe-490.522", "Fe I 490.522", 490.5220, Unit::Nanometer, 0.01333333 },
                { "Fe-495.730", "Fe I 495.730", 495.7300, Unit::Nanometer, 0.17000000 },
                { "Fe-500.571", "Fe I 500.571", 500.5710, Unit::Nanometer, 0.12000000 },
                { "Fe-505.464", "Fe I 505.464", 505.4640, Unit::Nanometer, 0.00233333 },
                { "Fe-510.745", "Fe I 510.745", 510.7450, Unit::Nanometer, 0.09000000 },
                { "Fe-515.191", "Fe I 515.191", 515.1910, Unit::Nanometer, 0.04333333 },
                { "Fe-520.458", "Fe I 520.458", 520.4580, Unit::Nanometer, 0.07000000 },
                { "Fe-525.197", "Fe I 525.197", 525.1970, Unit::Nanometer, 0.00666667 },
                { "Fe-530.736", "Fe I 530.736", 530.7360, Unit::Nanometer, 0.06333333 },
                { "Fe-535.377", "Fe I 535.377", 535.3770, Unit::Nanometer, 0.00186667 },
                { "Fe-540.412", "Fe I 540.412", 540.4120, Unit::Nanometer, 0.00666667 },
                { "Fe-545.561", "Fe I 545.561", 545.5610, Unit::Nanometer, 0.32000000 },
                { "Fe-550.620", "Fe I 550.620", 550.6200, Unit::Nanometer, 0.00493333 },
                { "Fe-555.489", "Fe I 555.489", 555.4890, Unit::Nanometer, 0.07000000 },
                { "Fe-560.277", "Fe I 560.277", 560.2770, Unit::Nanometer, 0.00540000 },
                { "Fe-565.640", "Fe I 565.640", 565.6400, Unit::Nanometer, 0.00113333 },
                { "Fe-570.599", "Fe I 570.599", 570.5990, Unit::Nanometer, 0.01000000 },
                { "Fe-575.312", "Fe I 575.312", 575.3120, Unit::Nanometer, 0.03333333 },
                { "Fe-580.672", "Fe I 580.672", 580.6720, Unit::Nanometer, 0.03333333 },
                { "Fe-585.609", "Fe I 585.609", 585.6090, Unit::Nanometer, 0.01000000 },
                { "Fe-590.567", "Fe I 590.567", 590.5670, Unit::Nanometer, 0.00666667 },
                { "Fe-595.361", "Fe I 595.361", 595.3610, Unit::Nanometer, 0.00253333 },
                { "Fe-600.301", "Fe I 600.301", 600.3010, Unit::Nanometer, 0.01000000 },
                { "Fe-605.600", "Fe I 605.600", 605.6000, Unit::Nanometer, 0.00426667 },
                { "Fe-610.318", "Fe I 610.318", 610.3180, Unit::Nanometer, 0.00666667 },
                { "Fe-615.773", "Fe I 615.773", 615.7730, Unit::Nanometer, 0.00666667 },
                { "Fe-620.837", "Fe I 620.837", 620.8370, Unit::Nanometer, 0.00126667 },
                { "Fe-625.426", "Fe I 625.426", 625.4260, Unit::Nanometer, 0.00626667 },
                { "Fe-630.249", "Fe I 630.249", 630.2490, Unit::Nanometer, 0.01333333 },
                { "Fe-635.870", "Fe I 635.870", 635.8700, Unit::Nanometer, 0.02333333 },
                { "Fe-640.802", "Fe I 640.802", 640.8020, Unit::Nanometer, 0.01000000 },
                { "Fe-645.638", "Fe I 645.638", 645.6380, Unit::Nanometer, 0.00233333 },
                { "Fe-651.837", "Fe I 651.837", 651.8370, Unit::Nanometer, 0.00300000 },
                { "Fe-656.922", "Fe I 656.922", 656.9220, Unit::Nanometer, 0.02000000 },
                { "Fe-661.382", "Fe I 661.382", 661.3820, Unit::Nanometer, 0.00113333 },
                { "Fe-666.344", "Fe I 666.344", 666.3440, Unit::Nanometer, 0.03666667 },
                { "Fe-671.374", "Fe I 671.374", 671.3740, Unit::Nanometer, 0.00360000 },
                { "Fe-676.411", "Fe I 676.411", 676.4110, Unit::Nanometer, 0.00120000 },
                { "Fe-681.026", "Fe I 681.026", 681.0260, Unit::Nanometer, 0.03333333 },
                { "Fe-686.251", "Fe I 686.251", 686.2510, Unit::Nanometer, 0.01000000 },
                { "Fe-691.668", "Fe I 691.668", 691.6680, Unit::Nanometer, 0.03000000 },
                { "Fe-697.885", "Fe I 697.885", 697.8850, Unit::Nanometer, 0.05333333 },
            };
            return lines;
        }

        const std::vector<AtomicScaleBuilder::SourceLine>& cobaltLines()
        {
            using Unit = AtomicScaleBuilder::WavelengthUnit;
            static const std::vector<AtomicScaleBuilder::SourceLine> lines {
                { "Co-380.810", "Co I 380.810", 380.8100, Unit::Nanometer, 0.13800000 },
                { "Co-385.094", "Co I 385.094", 385.0940, Unit::Nanometer, 0.06000000 },
                { "Co-390.993", "Co I 390.993", 390.9930, Unit::Nanometer, 0.08400000 },
                { "Co-395.291", "Co I 395.291", 395.2910, Unit::Nanometer, 0.03400000 },
                { "Co-401.671", "Co I 401.671", 401.6710, Unit::Nanometer, 0.00144000 },
                { "Co-406.637", "Co I 406.637", 406.6370, Unit::Nanometer, 0.00196000 },
                { "Co-411.822", "Co I 411.822", 411.8220, Unit::Nanometer, 0.00284000 },
                { "Co-416.065", "Co I 416.065", 416.0650, Unit::Nanometer, 0.07200000 },
                { "Co-424.425", "Co I 424.425", 424.4250, Unit::Nanometer, 0.04000000 },
                { "Co-429.818", "Co I 429.818", 429.8180, Unit::Nanometer, 0.03200000 },
                { "Co-435.330", "Co I 435.330", 435.3300, Unit::Nanometer, 0.00800000 },
                { "Co-440.979", "Co I 440.979", 440.9790, Unit::Nanometer, 0.00600000 },
                { "Co-445.619", "Co I 445.619", 445.6190, Unit::Nanometer, 0.00344000 },
                { "Co-450.056", "Co I 450.056", 450.0560, Unit::Nanometer, 0.00292000 },
                { "Co-455.333", "Co I 455.333", 455.3330, Unit::Nanometer, 0.01600000 },
                { "Co-461.628", "Co I 461.628", 461.6280, Unit::Nanometer, 0.00400000 },
                { "Co-466.341", "Co I 466.341", 466.3410, Unit::Nanometer, 0.80000000 },
                { "Co-473.621", "Co I 473.621", 473.6210, Unit::Nanometer, 0.00400000 },
                { "Co-478.589", "Co I 478.589", 478.5890, Unit::Nanometer, 0.00128000 },
                { "Co-483.346", "Co I 483.346", 483.3460, Unit::Nanometer, 0.00300000 },
                { "Co-488.545", "Co I 488.545", 488.5450, Unit::Nanometer, 0.00400000 },
                { "Co-493.624", "Co I 493.624", 493.6240, Unit::Nanometer, 0.00320000 },
                { "Co-498.696", "Co I 498.696", 498.6960, Unit::Nanometer, 0.00400000 },
                { "Co-503.758", "Co I 503.758", 503.7580, Unit::Nanometer, 0.00600000 },
                { "Co-508.362", "Co I 508.362", 508.3620, Unit::Nanometer, 0.04200000 },
                { "Co-513.568", "Co I 513.568", 513.5680, Unit::Nanometer, 0.20400000 },
                { "Co-518.531", "Co I 518.531", 518.5310, Unit::Nanometer, 0.00600000 },
                { "Co-523.521", "Co I 523.521", 523.5210, Unit::Nanometer, 0.04800000 },
                { "Co-528.065", "Co I 528.065", 528.0650, Unit::Nanometer, 0.25200000 },
                { "Co-533.285", "Co I 533.285", 533.2850, Unit::Nanometer, 0.00600000 },
                { "Co-538.262", "Co I 538.262", 538.2620, Unit::Nanometer, 0.00600000 },
                { "Co-543.770", "Co I 543.770", 543.7700, Unit::Nanometer, 0.00400000 },
                { "Co-548.396", "Co I 548.396", 548.3960, Unit::Nanometer, 0.10800000 },
                { "Co-553.077", "Co I 553.077", 553.0770, Unit::Nanometer, 0.07200000 },
                { "Co-559.190", "Co I 559.190", 559.1900, Unit::Nanometer, 0.00136000 },
                { "Co-564.722", "Co I 564.722", 564.7220, Unit::Nanometer, 0.10800000 },
                { "Co-572.553", "Co I 572.553", 572.5530, Unit::Nanometer, 0.00132000 },
                { "Co-577.185", "Co I 577.185", 577.1850, Unit::Nanometer, 0.01400000 },
                { "Co-582.685", "Co I 582.685", 582.6850, Unit::Nanometer, 0.00800000 },
                { "Co-588.341", "Co I 588.341", 588.3410, Unit::Nanometer, 0.00140000 },
                { "Co-593.538", "Co I 593.538", 593.5380, Unit::Nanometer, 0.06000000 },
                { "Co-598.408", "Co I 598.408", 598.4080, Unit::Nanometer, 0.00264000 },
                { "Co-603.183", "Co I 603.183", 603.1830, Unit::Nanometer, 0.02200000 },
                { "Co-608.244", "Co I 608.244", 608.2440, Unit::Nanometer, 0.26400000 },
                { "Co-615.819", "Co I 615.819", 615.8190, Unit::Nanometer, 0.01400000 },
                { "Co-620.572", "Co I 620.572", 620.5720, Unit::Nanometer, 0.04800000 },
                { "Co-625.700", "Co I 625.700", 625.7000, Unit::Nanometer, 0.00136000 },
                { "Co-635.771", "Co I 635.771", 635.7710, Unit::Nanometer, 0.01600000 },
                { "Co-641.778", "Co I 641.778", 641.7780, Unit::Nanometer, 0.10800000 },
                { "Co-646.546", "Co I 646.546", 646.5460, Unit::Nanometer, 0.00120000 },
                { "Co-652.211", "Co I 652.211", 652.2110, Unit::Nanometer, 0.06000000 },
                { "Co-657.622", "Co I 657.622", 657.6220, Unit::Nanometer, 0.07800000 },
                { "Co-662.822", "Co I 662.822", 662.8220, Unit::Nanometer, 0.03400000 },
                { "Co-667.880", "Co I 667.880", 667.8800, Unit::Nanometer, 0.12600000 },
                { "Co-677.103", "Co I 677.103", 677.1030, Unit::Nanometer, 0.35400000 },
                { "Co-683.338", "Co I 683.338", 683.3380, Unit::Nanometer, 0.02200000 },
                { "Co-689.630", "Co I 689.630", 689.6300, Unit::Nanometer, 0.00132000 },
                { "Co-699.732", "Co I 699.732", 699.7320, Unit::Nanometer, 0.11400000 },
            };
            return lines;
        }

        const std::vector<AtomicScaleBuilder::SourceLine>& nickelLines()
        {
            using Unit = AtomicScaleBuilder::WavelengthUnit;
            static const std::vector<AtomicScaleBuilder::SourceLine> lines {
                { "Ni-383.287", "Ni I 383.287", 383.2870, Unit::Nanometer, 0.01600000 },
                { "Ni-388.967", "Ni I 388.967", 388.9670, Unit::Nanometer, 0.01000000 },
                { "Ni-397.356", "Ni I 397.356", 397.3560, Unit::Nanometer, 0.08400000 },
                { "Ni-411.720", "Ni I 411.720", 411.7200, Unit::Nanometer, 0.00296000 },
                { "Ni-416.047", "Ni I 416.047", 416.0470, Unit::Nanometer, 0.00600000 },
                { "Ni-423.468", "Ni I 423.468", 423.4680, Unit::Nanometer, 0.00172000 },
                { "Ni-428.531", "Ni I 428.531", 428.5310, Unit::Nanometer, 0.00172000 },
                { "Ni-433.164", "Ni I 433.164", 433.1640, Unit::Nanometer, 0.15000000 },
                { "Ni-438.287", "Ni I 438.287", 438.2870, Unit::Nanometer, 0.00104000 },
                { "Ni-443.757", "Ni I 443.757", 443.7570, Unit::Nanometer, 0.01000000 },
                { "Ni-448.521", "Ni I 448.521", 448.5210, Unit::Nanometer, 0.00316000 },
                { "Ni-453.793", "Ni I 453.793", 453.7930, Unit::Nanometer, 0.00124000 },
                { "Ni-459.684", "Ni I 459.684", 459.6840, Unit::Nanometer, 0.00136000 },
                { "Ni-464.866", "Ni I 464.866", 464.8660, Unit::Nanometer, 0.39600000 },
                { "Ni-469.839", "Ni I 469.839", 469.8390, Unit::Nanometer, 0.03000000 },
                { "Ni-474.016", "Ni I 474.016", 474.0160, Unit::Nanometer, 0.01400000 },
                { "Ni-479.343", "Ni I 479.343", 479.3430, Unit::Nanometer, 0.00104000 },
                { "Ni-484.316", "Ni I 484.316", 484.3160, Unit::Nanometer, 0.02000000 },
                { "Ni-490.100", "Ni I 490.100", 490.1000, Unit::Nanometer, 0.00124000 },
                { "Ni-495.321", "Ni I 495.321", 495.3210, Unit::Nanometer, 0.16800000 },
                { "Ni-500.374", "Ni I 500.374", 500.3740, Unit::Nanometer, 0.02800000 },
                { "Ni-505.986", "Ni I 505.986", 505.9860, Unit::Nanometer, 0.00180000 },
                { "Ni-510.297", "Ni I 510.297", 510.2970, Unit::Nanometer, 0.09600000 },
                { "Ni-515.576", "Ni I 515.576", 515.5760, Unit::Nanometer, 0.16200000 },
                { "Ni-522.029", "Ni I 522.029", 522.0290, Unit::Nanometer, 0.03200000 },
                { "Ni-527.582", "Ni I 527.582", 527.5820, Unit::Nanometer, 0.00400000 },
                { "Ni-532.160", "Ni I 532.160", 532.1600, Unit::Nanometer, 0.00148000 },
                { "Ni-537.133", "Ni I 537.133", 537.1330, Unit::Nanometer, 0.06000000 },
                { "Ni-542.464", "Ni I 542.464", 542.4640, Unit::Nanometer, 0.02800000 },
                { "Ni-547.691", "Ni I 547.691", 547.6910, Unit::Nanometer, 0.60000000 },
                { "Ni-553.711", "Ni I 553.711", 553.7110, Unit::Nanometer, 0.00108000 },
                { "Ni-558.936", "Ni I 558.936", 558.9360, Unit::Nanometer, 0.02000000 },
                { "Ni-563.875", "Ni I 563.875", 563.8750, Unit::Nanometer, 0.00260000 },
                { "Ni-568.220", "Ni I 568.220", 568.2200, Unit::Nanometer, 0.09600000 },
                { "Ni-574.835", "Ni I 574.835", 574.8350, Unit::Nanometer, 0.03000000 },
                { "Ni-580.910", "Ni I 580.910", 580.9100, Unit::Nanometer, 0.00100000 },
                { "Ni-585.776", "Ni I 585.776", 585.7760, Unit::Nanometer, 0.10800000 },
                { "Ni-590.540", "Ni I 590.540", 590.5400, Unit::Nanometer, 0.03200000 },
                { "Ni-596.420", "Ni I 596.420", 596.4200, Unit::Nanometer, 0.05400000 },
                { "Ni-602.575", "Ni I 602.575", 602.5750, Unit::Nanometer, 0.00340000 },
                { "Ni-608.628", "Ni I 608.628", 608.6280, Unit::Nanometer, 0.06600000 },
                { "Ni-613.014", "Ni I 613.014", 613.0140, Unit::Nanometer, 0.00364000 },
                { "Ni-618.671", "Ni I 618.671", 618.6710, Unit::Nanometer, 0.00800000 },
                { "Ni-625.636", "Ni I 625.636", 625.6360, Unit::Nanometer, 0.07200000 },
                { "Ni-631.466", "Ni I 631.466", 631.4660, Unit::Nanometer, 0.06600000 },
                { "Ni-637.522", "Ni I 637.522", 637.5220, Unit::Nanometer, 0.00108000 },
                { "Ni-642.151", "Ni I 642.151", 642.1510, Unit::Nanometer, 0.00600000 },
                { "Ni-648.958", "Ni I 648.958", 648.9580, Unit::Nanometer, 0.00176000 },
                { "Ni-653.380", "Ni I 653.380", 653.3800, Unit::Nanometer, 0.00184000 },
                { "Ni-658.631", "Ni I 658.631", 658.6310, Unit::Nanometer, 0.02600000 },
                { "Ni-663.512", "Ni I 663.512", 663.5120, Unit::Nanometer, 0.00136000 },
                { "Ni-676.777", "Ni I 676.777", 676.7770, Unit::Nanometer, 0.04800000 },
                { "Ni-681.357", "Ni I 681.357", 681.3570, Unit::Nanometer, 0.00600000 },
                { "Ni-691.456", "Ni I 691.456", 691.4560, Unit::Nanometer, 0.11400000 },
            };
            return lines;
        }

        const std::vector<AtomicScaleBuilder::SourceLine>& copperLines()
        {
            using Unit = AtomicScaleBuilder::WavelengthUnit;
            static const std::vector<AtomicScaleBuilder::SourceLine> lines {
                { "Cu-380.523", "Cu I 380.523", 380.5230, Unit::Nanometer, 0.01333333 },
                { "Cu-385.110", "Cu I 385.110", 385.1100, Unit::Nanometer, 0.01000000 },
                { "Cu-390.318", "Cu I 390.318", 390.3180, Unit::Nanometer, 0.00160000 },
                { "Cu-395.847", "Cu I 395.847", 395.8470, Unit::Nanometer, 0.00133333 },
                { "Cu-400.348", "Cu I 400.348", 400.3480, Unit::Nanometer, 0.00186667 },
                { "Cu-405.062", "Cu I 405.062", 405.0620, Unit::Nanometer, 0.00166667 },
                { "Cu-410.422", "Cu I 410.422", 410.4220, Unit::Nanometer, 0.01000000 },
                { "Cu-416.428", "Cu I 416.428", 416.4280, Unit::Nanometer, 0.00126667 },
                { "Cu-421.691", "Cu I 421.691", 421.6910, Unit::Nanometer, 0.00266667 },
                { "Cu-426.761", "Cu I 426.761", 426.7610, Unit::Nanometer, 0.00220000 },
                { "Cu-432.076", "Cu I 432.076", 432.0760, Unit::Nanometer, 0.00113333 },
                { "Cu-437.580", "Cu I 437.580", 437.5800, Unit::Nanometer, 0.00600000 },
                { "Cu-444.088", "Cu I 444.088", 444.0880, Unit::Nanometer, 0.00166667 },
                { "Cu-450.735", "Cu I 450.735", 450.7350, Unit::Nanometer, 0.04000000 },
                { "Cu-455.751", "Cu I 455.751", 455.7510, Unit::Nanometer, 0.00400000 },
                { "Cu-460.942", "Cu I 460.942", 460.9420, Unit::Nanometer, 0.00140000 },
                { "Cu-465.112", "Cu I 465.112", 465.1120, Unit::Nanometer, 0.26000000 },
                { "Cu-470.459", "Cu I 470.459", 470.4590, Unit::Nanometer, 0.11000000 },
                { "Cu-475.843", "Cu I 475.843", 475.8430, Unit::Nanometer, 0.00506667 },
                { "Cu-480.566", "Cu I 480.566", 480.5660, Unit::Nanometer, 0.02333333 },
                { "Cu-485.499", "Cu I 485.499", 485.4990, Unit::Nanometer, 0.00666667 },
                { "Cu-490.714", "Cu I 490.714", 490.7140, Unit::Nanometer, 0.00666667 },
                { "Cu-495.372", "Cu I 495.372", 495.3720, Unit::Nanometer, 0.01333333 },
                { "Cu-500.680", "Cu I 500.680", 500.6800, Unit::Nanometer, 0.02333333 },
                { "Cu-505.478", "Cu I 505.478", 505.4780, Unit::Nanometer, 0.01000000 },
                { "Cu-510.554", "Cu I 510.554", 510.5540, Unit::Nanometer, 0.43000000 },
                { "Cu-515.726", "Cu I 515.726", 515.7260, Unit::Nanometer, 0.00466667 },
                { "Cu-520.714", "Cu I 520.714", 520.7140, Unit::Nanometer, 0.01666667 },
                { "Cu-525.421", "Cu I 525.421", 525.4210, Unit::Nanometer, 0.00246667 },
                { "Cu-531.599", "Cu I 531.599", 531.5990, Unit::Nanometer, 0.00146667 },
                { "Cu-536.624", "Cu I 536.624", 536.6240, Unit::Nanometer, 0.00113333 },
                { "Cu-541.848", "Cu I 541.848", 541.8480, Unit::Nanometer, 0.00113333 },
                { "Cu-546.856", "Cu I 546.856", 546.8560, Unit::Nanometer, 0.00153333 },
                { "Cu-552.720", "Cu I 552.720", 552.7200, Unit::Nanometer, 0.00666667 },
                { "Cu-557.844", "Cu I 557.844", 557.8440, Unit::Nanometer, 0.00326667 },
                { "Cu-562.170", "Cu I 562.170", 562.1700, Unit::Nanometer, 0.00273333 },
                { "Cu-567.600", "Cu I 567.600", 567.6000, Unit::Nanometer, 0.00560000 },
                { "Cu-572.178", "Cu I 572.178", 572.1780, Unit::Nanometer, 0.01000000 },
                { "Cu-578.392", "Cu I 578.392", 578.3920, Unit::Nanometer, 0.01000000 },
                { "Cu-583.351", "Cu I 583.351", 583.3510, Unit::Nanometer, 0.00240000 },
                { "Cu-589.797", "Cu I 589.797", 589.7970, Unit::Nanometer, 0.01333333 },
                { "Cu-594.183", "Cu I 594.183", 594.1830, Unit::Nanometer, 0.00420000 },
                { "Cu-599.559", "Cu I 599.559", 599.5590, Unit::Nanometer, 0.00340000 },
                { "Cu-607.222", "Cu I 607.222", 607.2220, Unit::Nanometer, 0.00666667 },
                { "Cu-612.773", "Cu I 612.773", 612.7730, Unit::Nanometer, 0.05666667 },
                { "Cu-617.430", "Cu I 617.430", 617.4300, Unit::Nanometer, 0.00126667 },
                { "Cu-622.129", "Cu I 622.129", 622.1290, Unit::Nanometer, 0.00386667 },
                { "Cu-627.666", "Cu I 627.666", 627.6660, Unit::Nanometer, 0.00666667 },
                { "Cu-632.647", "Cu I 632.647", 632.6470, Unit::Nanometer, 0.01333333 },
                { "Cu-637.725", "Cu I 637.725", 637.7250, Unit::Nanometer, 0.06000000 },
                { "Cu-642.757", "Cu I 642.757", 642.7570, Unit::Nanometer, 0.00113333 },
                { "Cu-647.543", "Cu I 647.543", 647.5430, Unit::Nanometer, 0.00200000 },
                { "Cu-652.382", "Cu I 652.382", 652.3820, Unit::Nanometer, 0.01000000 },
                { "Cu-657.708", "Cu I 657.708", 657.7080, Unit::Nanometer, 0.02000000 },
                { "Cu-662.624", "Cu I 662.624", 662.6240, Unit::Nanometer, 0.00486667 },
                { "Cu-667.223", "Cu I 667.223", 667.2230, Unit::Nanometer, 0.01333333 },
                { "Cu-673.640", "Cu I 673.640", 673.6400, Unit::Nanometer, 0.04000000 },
                { "Cu-678.648", "Cu I 678.648", 678.6480, Unit::Nanometer, 0.00106667 },
                { "Cu-683.546", "Cu I 683.546", 683.5460, Unit::Nanometer, 0.00193333 },
                { "Cu-688.992", "Cu I 688.992", 688.9920, Unit::Nanometer, 0.01000000 },
                { "Cu-693.755", "Cu I 693.755", 693.7550, Unit::Nanometer, 0.04666667 },
                { "Cu-698.655", "Cu I 698.655", 698.6550, Unit::Nanometer, 0.02333333 },
            };
            return lines;
        }

        const std::vector<AtomicScaleBuilder::SourceLine>& zincLines()
        {
            using Unit = AtomicScaleBuilder::WavelengthUnit;
            static const std::vector<AtomicScaleBuilder::SourceLine> lines {
                { "Zn-384.029", "Zn I 384.029", 384.0290, Unit::Nanometer, 0.05400000 },
                { "Zn-396.543", "Zn I 396.543", 396.5430, Unit::Nanometer, 0.01600000 },
                { "Zn-407.814", "Zn I 407.814", 407.8140, Unit::Nanometer, 0.00500000 },
                { "Zn-429.832", "Zn I 429.832", 429.8320, Unit::Nanometer, 0.10200000 },
                { "Zn-455.549", "Zn I 455.549", 455.5490, Unit::Nanometer, 0.00900000 },
                { "Zn-460.422", "Zn I 460.422", 460.4220, Unit::Nanometer, 0.00300000 },
                { "Zn-466.559", "Zn I 466.559", 466.5590, Unit::Nanometer, 0.00188000 },
                { "Zn-472.527", "Zn I 472.527", 472.5270, Unit::Nanometer, 0.00200000 },
                { "Zn-477.071", "Zn I 477.071", 477.0710, Unit::Nanometer, 0.00158000 },
                { "Zn-491.163", "Zn I 491.163", 491.1630, Unit::Nanometer, 0.03000000 },
                { "Zn-506.950", "Zn I 506.950", 506.9500, Unit::Nanometer, 0.00500000 },
                { "Zn-518.198", "Zn I 518.198", 518.1980, Unit::Nanometer, 0.05400000 },
                { "Zn-530.864", "Zn I 530.864", 530.8640, Unit::Nanometer, 0.00300000 },
                { "Zn-577.550", "Zn I 577.550", 577.5500, Unit::Nanometer, 0.00300000 },
                { "Zn-589.436", "Zn I 589.436", 589.4360, Unit::Nanometer, 0.01200000 },
                { "Zn-602.118", "Zn I 602.118", 602.1180, Unit::Nanometer, 0.00700000 },
                { "Zn-610.249", "Zn I 610.249", 610.2490, Unit::Nanometer, 0.01100000 },
                { "Zn-621.458", "Zn I 621.458", 621.4580, Unit::Nanometer, 0.00800000 },
                { "Zn-636.235", "Zn I 636.235", 636.2350, Unit::Nanometer, 0.50000000 },
                { "Zn-647.918", "Zn I 647.918", 647.9180, Unit::Nanometer, 0.00500000 },
                { "Zn-692.832", "Zn I 692.832", 692.8320, Unit::Nanometer, 0.00900000 },
            };
            return lines;
        }

	    const ScaleDef& getScaleDef (int mode)
	    {
	        return kScales[(size_t) juce::jlimit(0, (int) kScales.size() - 1, mode)];
	    }

	    bool isSpectralMode (int mode) noexcept
	    {
	        return mode >= kHydrogenMode && mode <= kZincMode;
	    }

	    const char* spectralName (int mode) noexcept
	    {
	        if (mode == kHydrogenMode) return "Hydrogen Spectrum";
	        if (mode == kHeliumMode) return "Helium Spectrum";
            if (mode == kLithiumMode) return "Lithium Spectrum";
            if (mode == kBerylliumMode) return "Beryllium Spectrum";
            if (mode == kBoronMode) return "Boron Spectrum";
            if (mode == kCarbonMode) return "Carbon Spectrum";
            if (mode == kOxygenMode) return "Oxygen Spectrum";
            if (mode == kFluorineMode) return "Fluorine Spectrum";
            if (mode == kNeonMode) return "Neon Spectrum";
            if (mode == kSodiumMode) return "Sodium Spectrum";
            if (mode == kMagnesiumMode) return "Magnesium Spectrum";
            if (mode == kAluminiumMode) return "Aluminium Spectrum";
            if (mode == kSiliconMode) return "Silicon Spectrum";
            if (mode == kPhosphorusMode) return "Phosphorus Spectrum";
            if (mode == kSulfurMode) return "Sulfur Spectrum";
            if (mode == kChlorineMode) return "Chlorine Spectrum";
            if (mode == kArgonMode) return "Argon Spectrum";
            if (mode == kPotassiumMode) return "Potassium Spectrum";
            if (mode == kCalciumMode) return "Calcium Spectrum";
            if (mode == kScandiumMode) return "Scandium Spectrum";
            if (mode == kTitaniumMode) return "Titanium Spectrum";
            if (mode == kVanadiumMode) return "Vanadium Spectrum";
            if (mode == kChromiumMode) return "Chromium Spectrum";
            if (mode == kManganeseMode) return "Manganese Spectrum";
            if (mode == kIronMode) return "Iron Spectrum";
            if (mode == kCobaltMode) return "Cobalt Spectrum";
            if (mode == kNickelMode) return "Nickel Spectrum";
            if (mode == kCopperMode) return "Copper Spectrum";
            if (mode == kZincMode) return "Zinc Spectrum";
	        return "";
	    }

        int elementForSpectralMode (int mode) noexcept
        {
            return juce::jlimit(kElementHydrogen, kLastElement, mode - kHydrogenMode);
        }

        AtomicScaleBuilder::ScaleMode scaleModeForAtomicIndex (int index) noexcept
        {
            switch (juce::jlimit(0, 4, index))
            {
                case 0:  return AtomicScaleBuilder::ScaleMode::Melodic;
                case 2:  return AtomicScaleBuilder::ScaleMode::Microtonal;
                case 3:  return AtomicScaleBuilder::ScaleMode::Scientific;
                case 4:  return AtomicScaleBuilder::ScaleMode::Raw;
                default: return AtomicScaleBuilder::ScaleMode::Performable;
            }
        }

        const char* atomicScaleModeName (int index) noexcept
        {
            switch (juce::jlimit(0, 4, index))
            {
                case 0:  return "Core";
                case 2:  return "Microtonal";
                case 3:  return "Scientific";
                case 4:  return "Raw";
                default: return "Extended";
            }
        }

        const char* samplePlaybackName (int index) noexcept
        {
            return juce::jlimit(kSamplePlaybackDirect, kSamplePlaybackGranular, index) == kSamplePlaybackGranular
                ? "Granular"
                : "Sample Player";
        }

        const std::vector<AtomicScaleBuilder::SourceLine>& sourceLinesForElement (int element)
        {
            const int safeElement = juce::jlimit(kElementHydrogen, kLastElement, element);
            const auto& generatedLines = ElementSpectralData::linesForElement(safeElement);
            if (! generatedLines.empty())
                return generatedLines;

            switch (safeElement)
            {
		                case kElementHydrogen:   return hydrogenLines();
		                case kElementLithium:    return lithiumLines();
	                case kElementBeryllium:  return berylliumLines();
                    case kElementBoron:      return boronLines();
                    case kElementCarbon:     return carbonLines();
                    case kElementOxygen:     return oxygenLines();
                    case kElementFluorine:   return fluorineLines();
                    case kElementNeon:       return neonLines();
                    case kElementSodium:     return sodiumLines();
                    case kElementMagnesium:  return magnesiumLines();
                    case kElementAluminium:  return aluminiumLines();
                    case kElementSilicon:    return siliconLines();
                    case kElementPhosphorus: return phosphorusLines();
                    case kElementSulfur:     return sulfurLines();
                    case kElementChlorine:   return chlorineLines();
                    case kElementArgon:      return argonLines();
                    case kElementPotassium:  return potassiumLines();
                    case kElementCalcium:    return calciumLines();
                    case kElementScandium:   return scandiumLines();
                    case kElementTitanium:   return titaniumLines();
                    case kElementVanadium:   return vanadiumLines();
                    case kElementChromium:   return chromiumLines();
                    case kElementManganese:  return manganeseLines();
                    case kElementIron:       return ironLines();
                    case kElementCobalt:     return cobaltLines();
                    case kElementNickel:     return nickelLines();
                    case kElementCopper:     return copperLines();
                    case kElementZinc:       return zincLines();
		                case kElementHelium:
		                default:                 return heliumLines();
		            }
        }

        const char* elementName (int element) noexcept
        {
            switch (juce::jlimit(kElementHydrogen, kLastElement, element))
            {
	                case kElementHydrogen:  return "Hydrogen";
	                case kElementLithium:   return "Lithium";
	                case kElementBeryllium: return "Beryllium";
                    case kElementBoron:     return "Boron";
                    case kElementCarbon:    return "Carbon";
                    case kElementOxygen:    return "Oxygen";
                    case kElementFluorine:  return "Fluorine";
                    case kElementNeon:      return "Neon";
                    case kElementSodium:    return "Sodium";
                    case kElementMagnesium: return "Magnesium";
                    case kElementAluminium: return "Aluminium";
                    case kElementSilicon:   return "Silicon";
                    case kElementPhosphorus: return "Phosphorus";
                    case kElementSulfur:    return "Sulfur";
                    case kElementChlorine:  return "Chlorine";
                    case kElementArgon:     return "Argon";
                    case kElementPotassium: return "Potassium";
                    case kElementCalcium:   return "Calcium";
                    case kElementScandium:  return "Scandium";
                    case kElementTitanium:  return "Titanium";
                    case kElementVanadium:  return "Vanadium";
                    case kElementChromium:  return "Chromium";
                    case kElementManganese: return "Manganese";
                    case kElementIron:      return "Iron";
                    case kElementCobalt:    return "Cobalt";
                    case kElementNickel:    return "Nickel";
                    case kElementCopper:    return "Copper";
                    case kElementZinc:      return "Zinc";
		                case kElementHelium:
		                default:                return "Helium";
		            }
        }

        AtomicScaleBuilder::Result buildAtomicResult (int element, AtomicScaleBuilder::ScaleMode mode)
        {
            AtomicScaleBuilder::Options options;
            options.elementName = elementName(element);
            options.scaleMode = mode;
            options.representativeMode = AtomicScaleBuilder::RepresentativeMode::Medoid;
            options.alwaysIncludeRoot = true;
            return AtomicScaleBuilder::buildPlayableAtomicScale(sourceLinesForElement(element), options);
        }

        // One element's five scale-mode builds. Member order is load-bearing:
        // atomicResultFor() maps each ScaleMode enum to the matching member below.
        struct ResultSet
        {
            AtomicScaleBuilder::Result melodic, performable, microtonal, scientific, raw;
        };

        const AtomicScaleBuilder::Result& atomicResultFor (const std::array<ResultSet, kLastElement + 1>& cache,
                                                           int element, AtomicScaleBuilder::ScaleMode mode)
        {
            const auto& set = cache[(size_t) juce::jlimit(kElementHydrogen, kLastElement, element)];
            switch (mode)
            {
                case AtomicScaleBuilder::ScaleMode::Melodic:     return set.melodic;
                case AtomicScaleBuilder::ScaleMode::Microtonal:  return set.microtonal;
                case AtomicScaleBuilder::ScaleMode::Scientific:  return set.scientific;
                case AtomicScaleBuilder::ScaleMode::Raw:         return set.raw;
                case AtomicScaleBuilder::ScaleMode::Performable: return set.performable;
            }

            return set.performable;
        }

        // Builds all 29 elements x 5 modes. Heavy (std::vector allocation + sort /
        // cluster per build); MUST run on the message thread only.
        std::array<ResultSet, kLastElement + 1> buildAtomicScaleResultSets()
        {
            return {{
                { buildAtomicResult(kElementHydrogen, AtomicScaleBuilder::ScaleMode::Melodic),
                  buildAtomicResult(kElementHydrogen, AtomicScaleBuilder::ScaleMode::Performable),
                  buildAtomicResult(kElementHydrogen, AtomicScaleBuilder::ScaleMode::Microtonal),
                  buildAtomicResult(kElementHydrogen, AtomicScaleBuilder::ScaleMode::Scientific),
                  buildAtomicResult(kElementHydrogen, AtomicScaleBuilder::ScaleMode::Raw) },
                { buildAtomicResult(kElementHelium, AtomicScaleBuilder::ScaleMode::Melodic),
                  buildAtomicResult(kElementHelium, AtomicScaleBuilder::ScaleMode::Performable),
                  buildAtomicResult(kElementHelium, AtomicScaleBuilder::ScaleMode::Microtonal),
                  buildAtomicResult(kElementHelium, AtomicScaleBuilder::ScaleMode::Scientific),
                  buildAtomicResult(kElementHelium, AtomicScaleBuilder::ScaleMode::Raw) },
                { buildAtomicResult(kElementLithium, AtomicScaleBuilder::ScaleMode::Melodic),
                  buildAtomicResult(kElementLithium, AtomicScaleBuilder::ScaleMode::Performable),
                  buildAtomicResult(kElementLithium, AtomicScaleBuilder::ScaleMode::Microtonal),
                  buildAtomicResult(kElementLithium, AtomicScaleBuilder::ScaleMode::Scientific),
                  buildAtomicResult(kElementLithium, AtomicScaleBuilder::ScaleMode::Raw) },
                { buildAtomicResult(kElementBeryllium, AtomicScaleBuilder::ScaleMode::Melodic),
                  buildAtomicResult(kElementBeryllium, AtomicScaleBuilder::ScaleMode::Performable),
                  buildAtomicResult(kElementBeryllium, AtomicScaleBuilder::ScaleMode::Microtonal),
                  buildAtomicResult(kElementBeryllium, AtomicScaleBuilder::ScaleMode::Scientific),
                  buildAtomicResult(kElementBeryllium, AtomicScaleBuilder::ScaleMode::Raw) },
                { buildAtomicResult(kElementBoron, AtomicScaleBuilder::ScaleMode::Melodic),
                  buildAtomicResult(kElementBoron, AtomicScaleBuilder::ScaleMode::Performable),
                  buildAtomicResult(kElementBoron, AtomicScaleBuilder::ScaleMode::Microtonal),
                  buildAtomicResult(kElementBoron, AtomicScaleBuilder::ScaleMode::Scientific),
                  buildAtomicResult(kElementBoron, AtomicScaleBuilder::ScaleMode::Raw) },
                { buildAtomicResult(kElementCarbon, AtomicScaleBuilder::ScaleMode::Melodic),
                  buildAtomicResult(kElementCarbon, AtomicScaleBuilder::ScaleMode::Performable),
                  buildAtomicResult(kElementCarbon, AtomicScaleBuilder::ScaleMode::Microtonal),
                  buildAtomicResult(kElementCarbon, AtomicScaleBuilder::ScaleMode::Scientific),
                  buildAtomicResult(kElementCarbon, AtomicScaleBuilder::ScaleMode::Raw) },
                { buildAtomicResult(kElementOxygen, AtomicScaleBuilder::ScaleMode::Melodic),
                  buildAtomicResult(kElementOxygen, AtomicScaleBuilder::ScaleMode::Performable),
                  buildAtomicResult(kElementOxygen, AtomicScaleBuilder::ScaleMode::Microtonal),
                  buildAtomicResult(kElementOxygen, AtomicScaleBuilder::ScaleMode::Scientific),
                  buildAtomicResult(kElementOxygen, AtomicScaleBuilder::ScaleMode::Raw) },
                { buildAtomicResult(kElementFluorine, AtomicScaleBuilder::ScaleMode::Melodic),
                  buildAtomicResult(kElementFluorine, AtomicScaleBuilder::ScaleMode::Performable),
                  buildAtomicResult(kElementFluorine, AtomicScaleBuilder::ScaleMode::Microtonal),
                  buildAtomicResult(kElementFluorine, AtomicScaleBuilder::ScaleMode::Scientific),
                  buildAtomicResult(kElementFluorine, AtomicScaleBuilder::ScaleMode::Raw) },
                { buildAtomicResult(kElementNeon, AtomicScaleBuilder::ScaleMode::Melodic),
                  buildAtomicResult(kElementNeon, AtomicScaleBuilder::ScaleMode::Performable),
                  buildAtomicResult(kElementNeon, AtomicScaleBuilder::ScaleMode::Microtonal),
                  buildAtomicResult(kElementNeon, AtomicScaleBuilder::ScaleMode::Scientific),
                  buildAtomicResult(kElementNeon, AtomicScaleBuilder::ScaleMode::Raw) },
                { buildAtomicResult(kElementSodium, AtomicScaleBuilder::ScaleMode::Melodic),
                  buildAtomicResult(kElementSodium, AtomicScaleBuilder::ScaleMode::Performable),
                  buildAtomicResult(kElementSodium, AtomicScaleBuilder::ScaleMode::Microtonal),
                  buildAtomicResult(kElementSodium, AtomicScaleBuilder::ScaleMode::Scientific),
                  buildAtomicResult(kElementSodium, AtomicScaleBuilder::ScaleMode::Raw) },
                { buildAtomicResult(kElementMagnesium, AtomicScaleBuilder::ScaleMode::Melodic),
                  buildAtomicResult(kElementMagnesium, AtomicScaleBuilder::ScaleMode::Performable),
                  buildAtomicResult(kElementMagnesium, AtomicScaleBuilder::ScaleMode::Microtonal),
                  buildAtomicResult(kElementMagnesium, AtomicScaleBuilder::ScaleMode::Scientific),
                  buildAtomicResult(kElementMagnesium, AtomicScaleBuilder::ScaleMode::Raw) },
                { buildAtomicResult(kElementAluminium, AtomicScaleBuilder::ScaleMode::Melodic),
                  buildAtomicResult(kElementAluminium, AtomicScaleBuilder::ScaleMode::Performable),
                  buildAtomicResult(kElementAluminium, AtomicScaleBuilder::ScaleMode::Microtonal),
                  buildAtomicResult(kElementAluminium, AtomicScaleBuilder::ScaleMode::Scientific),
                  buildAtomicResult(kElementAluminium, AtomicScaleBuilder::ScaleMode::Raw) },
                { buildAtomicResult(kElementSilicon, AtomicScaleBuilder::ScaleMode::Melodic),
                  buildAtomicResult(kElementSilicon, AtomicScaleBuilder::ScaleMode::Performable),
                  buildAtomicResult(kElementSilicon, AtomicScaleBuilder::ScaleMode::Microtonal),
                  buildAtomicResult(kElementSilicon, AtomicScaleBuilder::ScaleMode::Scientific),
                  buildAtomicResult(kElementSilicon, AtomicScaleBuilder::ScaleMode::Raw) },
                { buildAtomicResult(kElementPhosphorus, AtomicScaleBuilder::ScaleMode::Melodic),
                  buildAtomicResult(kElementPhosphorus, AtomicScaleBuilder::ScaleMode::Performable),
                  buildAtomicResult(kElementPhosphorus, AtomicScaleBuilder::ScaleMode::Microtonal),
                  buildAtomicResult(kElementPhosphorus, AtomicScaleBuilder::ScaleMode::Scientific),
                  buildAtomicResult(kElementPhosphorus, AtomicScaleBuilder::ScaleMode::Raw) },
                { buildAtomicResult(kElementSulfur, AtomicScaleBuilder::ScaleMode::Melodic),
                  buildAtomicResult(kElementSulfur, AtomicScaleBuilder::ScaleMode::Performable),
                  buildAtomicResult(kElementSulfur, AtomicScaleBuilder::ScaleMode::Microtonal),
                  buildAtomicResult(kElementSulfur, AtomicScaleBuilder::ScaleMode::Scientific),
                  buildAtomicResult(kElementSulfur, AtomicScaleBuilder::ScaleMode::Raw) },
                { buildAtomicResult(kElementChlorine, AtomicScaleBuilder::ScaleMode::Melodic),
                  buildAtomicResult(kElementChlorine, AtomicScaleBuilder::ScaleMode::Performable),
                  buildAtomicResult(kElementChlorine, AtomicScaleBuilder::ScaleMode::Microtonal),
                  buildAtomicResult(kElementChlorine, AtomicScaleBuilder::ScaleMode::Scientific),
                  buildAtomicResult(kElementChlorine, AtomicScaleBuilder::ScaleMode::Raw) },
                { buildAtomicResult(kElementArgon, AtomicScaleBuilder::ScaleMode::Melodic),
                  buildAtomicResult(kElementArgon, AtomicScaleBuilder::ScaleMode::Performable),
                  buildAtomicResult(kElementArgon, AtomicScaleBuilder::ScaleMode::Microtonal),
                  buildAtomicResult(kElementArgon, AtomicScaleBuilder::ScaleMode::Scientific),
                  buildAtomicResult(kElementArgon, AtomicScaleBuilder::ScaleMode::Raw) },
                { buildAtomicResult(kElementPotassium, AtomicScaleBuilder::ScaleMode::Melodic),
                  buildAtomicResult(kElementPotassium, AtomicScaleBuilder::ScaleMode::Performable),
                  buildAtomicResult(kElementPotassium, AtomicScaleBuilder::ScaleMode::Microtonal),
                  buildAtomicResult(kElementPotassium, AtomicScaleBuilder::ScaleMode::Scientific),
                  buildAtomicResult(kElementPotassium, AtomicScaleBuilder::ScaleMode::Raw) },
                { buildAtomicResult(kElementCalcium, AtomicScaleBuilder::ScaleMode::Melodic),
                  buildAtomicResult(kElementCalcium, AtomicScaleBuilder::ScaleMode::Performable),
                  buildAtomicResult(kElementCalcium, AtomicScaleBuilder::ScaleMode::Microtonal),
                  buildAtomicResult(kElementCalcium, AtomicScaleBuilder::ScaleMode::Scientific),
                  buildAtomicResult(kElementCalcium, AtomicScaleBuilder::ScaleMode::Raw) },
                { buildAtomicResult(kElementScandium, AtomicScaleBuilder::ScaleMode::Melodic),
                  buildAtomicResult(kElementScandium, AtomicScaleBuilder::ScaleMode::Performable),
                  buildAtomicResult(kElementScandium, AtomicScaleBuilder::ScaleMode::Microtonal),
                  buildAtomicResult(kElementScandium, AtomicScaleBuilder::ScaleMode::Scientific),
                  buildAtomicResult(kElementScandium, AtomicScaleBuilder::ScaleMode::Raw) },
                { buildAtomicResult(kElementTitanium, AtomicScaleBuilder::ScaleMode::Melodic),
                  buildAtomicResult(kElementTitanium, AtomicScaleBuilder::ScaleMode::Performable),
                  buildAtomicResult(kElementTitanium, AtomicScaleBuilder::ScaleMode::Microtonal),
                  buildAtomicResult(kElementTitanium, AtomicScaleBuilder::ScaleMode::Scientific),
                  buildAtomicResult(kElementTitanium, AtomicScaleBuilder::ScaleMode::Raw) },
                { buildAtomicResult(kElementVanadium, AtomicScaleBuilder::ScaleMode::Melodic),
                  buildAtomicResult(kElementVanadium, AtomicScaleBuilder::ScaleMode::Performable),
                  buildAtomicResult(kElementVanadium, AtomicScaleBuilder::ScaleMode::Microtonal),
                  buildAtomicResult(kElementVanadium, AtomicScaleBuilder::ScaleMode::Scientific),
                  buildAtomicResult(kElementVanadium, AtomicScaleBuilder::ScaleMode::Raw) },
                { buildAtomicResult(kElementChromium, AtomicScaleBuilder::ScaleMode::Melodic),
                  buildAtomicResult(kElementChromium, AtomicScaleBuilder::ScaleMode::Performable),
                  buildAtomicResult(kElementChromium, AtomicScaleBuilder::ScaleMode::Microtonal),
                  buildAtomicResult(kElementChromium, AtomicScaleBuilder::ScaleMode::Scientific),
                  buildAtomicResult(kElementChromium, AtomicScaleBuilder::ScaleMode::Raw) },
                { buildAtomicResult(kElementManganese, AtomicScaleBuilder::ScaleMode::Melodic),
                  buildAtomicResult(kElementManganese, AtomicScaleBuilder::ScaleMode::Performable),
                  buildAtomicResult(kElementManganese, AtomicScaleBuilder::ScaleMode::Microtonal),
                  buildAtomicResult(kElementManganese, AtomicScaleBuilder::ScaleMode::Scientific),
                  buildAtomicResult(kElementManganese, AtomicScaleBuilder::ScaleMode::Raw) },
                { buildAtomicResult(kElementIron, AtomicScaleBuilder::ScaleMode::Melodic),
                  buildAtomicResult(kElementIron, AtomicScaleBuilder::ScaleMode::Performable),
                  buildAtomicResult(kElementIron, AtomicScaleBuilder::ScaleMode::Microtonal),
                  buildAtomicResult(kElementIron, AtomicScaleBuilder::ScaleMode::Scientific),
                  buildAtomicResult(kElementIron, AtomicScaleBuilder::ScaleMode::Raw) },
                { buildAtomicResult(kElementCobalt, AtomicScaleBuilder::ScaleMode::Melodic),
                  buildAtomicResult(kElementCobalt, AtomicScaleBuilder::ScaleMode::Performable),
                  buildAtomicResult(kElementCobalt, AtomicScaleBuilder::ScaleMode::Microtonal),
                  buildAtomicResult(kElementCobalt, AtomicScaleBuilder::ScaleMode::Scientific),
                  buildAtomicResult(kElementCobalt, AtomicScaleBuilder::ScaleMode::Raw) },
                { buildAtomicResult(kElementNickel, AtomicScaleBuilder::ScaleMode::Melodic),
                  buildAtomicResult(kElementNickel, AtomicScaleBuilder::ScaleMode::Performable),
                  buildAtomicResult(kElementNickel, AtomicScaleBuilder::ScaleMode::Microtonal),
                  buildAtomicResult(kElementNickel, AtomicScaleBuilder::ScaleMode::Scientific),
                  buildAtomicResult(kElementNickel, AtomicScaleBuilder::ScaleMode::Raw) },
                { buildAtomicResult(kElementCopper, AtomicScaleBuilder::ScaleMode::Melodic),
                  buildAtomicResult(kElementCopper, AtomicScaleBuilder::ScaleMode::Performable),
                  buildAtomicResult(kElementCopper, AtomicScaleBuilder::ScaleMode::Microtonal),
                  buildAtomicResult(kElementCopper, AtomicScaleBuilder::ScaleMode::Scientific),
                  buildAtomicResult(kElementCopper, AtomicScaleBuilder::ScaleMode::Raw) },
                { buildAtomicResult(kElementZinc, AtomicScaleBuilder::ScaleMode::Melodic),
                  buildAtomicResult(kElementZinc, AtomicScaleBuilder::ScaleMode::Performable),
                  buildAtomicResult(kElementZinc, AtomicScaleBuilder::ScaleMode::Microtonal),
                  buildAtomicResult(kElementZinc, AtomicScaleBuilder::ScaleMode::Scientific),
                  buildAtomicResult(kElementZinc, AtomicScaleBuilder::ScaleMode::Raw) },
            }};
        }

        const AtomicScaleBuilder::Result& atomicScaleResultForMode (const std::array<ResultSet, kLastElement + 1>& cache,
                                                                    int spectralMode, int atomicModeIndex)
        {
            return atomicResultFor(cache,
                                   elementForSpectralMode(spectralMode),
                                   scaleModeForAtomicIndex(atomicModeIndex));
        }

        const AtomicScaleBuilder::Result& atomicRawResultForElement (const std::array<ResultSet, kLastElement + 1>& cache,
                                                                     int element)
        {
            return atomicResultFor(cache, element, AtomicScaleBuilder::ScaleMode::Raw);
        }

        int spectralLineCount (const std::array<ResultSet, kLastElement + 1>& cache,
                               int mode, int atomicModeIndex) noexcept
        {
            if (! isSpectralMode(mode))
                return 0;

            return (int) atomicScaleResultForMode(cache, mode, atomicModeIndex).scaleDegrees.size();
        }

        int elementLineCount (const std::array<ResultSet, kLastElement + 1>& cache, int element) noexcept
        {
            return (int) atomicRawResultForElement(cache, element).timbrePartials.size();
        }

        double elementReferenceWavelength (const std::array<ResultSet, kLastElement + 1>& cache, int element) noexcept
        {
            return atomicRawResultForElement(cache, element).lambdaRefNm;
        }

	    double midiToHz (int midi) noexcept
	    {
	        return 440.0 * std::pow(2.0, ((double) midi - 69.0) / 12.0);
	    }

	    int hzToNearestMidi (double hz) noexcept
	    {
	        if (hz <= 0.0)
	            return 0;

	        return juce::jlimit(0, 127,
	                            (int) std::round(69.0 + 12.0 * std::log2(hz / 440.0)));
	    }

	    juce::String midiNoteName (int midi)
	    {
	        static const char* names[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
	        const int oct = midi / 12 - 1;
	        return juce::String(names[((midi % 12) + 12) % 12]) + juce::String(oct);
	    }

	    float velocityToAudibleGain (float velocity) noexcept
	    {
	        return juce::jlimit(0.0f, 1.0f, velocity);
	    }

    struct ModeProfile
    {
        const char* name;
        float grainSizeMul;
        float densityOffset;
        float pitchSpreadOffset;
        float jitterMul;
        float reverseChance;
        float stereoBoost;
        float brightnessOffset;
        float reverbBoost;
        float delayBoost;
        float tapeBoost;
        float playbackMul;
        float attackMul;
        float releaseMul;
        float motionMul;
        bool  reverbFreeze;
    };

    constexpr std::array<ModeProfile, 5> kModes { {
        { "Choir Cloud",     1.45f, -0.08f, 0.20f, 0.72f, 0.02f, 0.08f, -0.04f, 0.18f, 0.02f, 0.04f, 1.00f, 1.35f, 1.65f, 0.75f, false },
        { "Glass Harmonics", 0.72f,  0.08f, 4.20f, 0.45f, 0.06f, 0.18f,  0.24f, 0.14f, 0.08f, 0.02f, 2.00f, 0.82f, 1.15f, 1.10f, false },
        { "Sub Swarm",       1.18f,  0.05f, 0.70f, 1.05f, 0.04f, 0.12f, -0.28f, 0.10f, 0.10f, 0.18f, 0.50f, 1.05f, 1.95f, 1.35f, false },
        { "Spectral Rain",   0.38f,  0.35f, 7.00f, 1.30f, 0.22f, 0.30f,  0.16f, 0.07f, 0.16f, 0.06f, 1.50f, 0.55f, 0.82f, 1.70f, false },
        { "Frozen Hall",     2.35f,  0.10f, 1.20f, 0.35f, 0.10f, 0.20f, -0.08f, 0.38f, 0.12f, 0.10f, 1.00f, 1.80f, 4.20f, 0.55f, true  },
    } };

    const ModeProfile& getModeProfile (int mode)
    {
        return kModes[(size_t) juce::jlimit(0, (int) kModes.size() - 1, mode)];
    }

    int floorDiv (int a, int b) noexcept
    {
        int q = a / b;
        const int r = a % b;
        if (r < 0)
            --q;
        return q;
    }

    float grainEnvelope (float t, int shape) noexcept
    {
        t = juce::jlimit(0.0f, 1.0f, t);
        switch (juce::jlimit(0, 3, shape))
        {
            case 1: // Triangle
                return 1.0f - std::abs(t * 2.0f - 1.0f);

            case 2: // Soft gate / Tukey-ish: flat middle, clean edges.
                if (t < 0.18f) return t / 0.18f;
                if (t > 0.82f) return (1.0f - t) / 0.18f;
                return 1.0f;

            case 3: // Pulse: sharper, more percussive grains.
                return std::pow(std::sin(kPi * t), 0.35f);

            default:
                return 0.5f * (1.0f - std::cos(kTwoPi * t));
        }
    }
}

// Engine-owned atomic-scale cache. Forward-declared in PartialEngine.h; the
// complete type lives here so the audio thread never sees the build machinery.
// Holds one ResultSet (5 scale modes) per element, indexed by element index
// exactly as the original function-local cache was.
struct AtomicScaleCache
{
    std::array<ResultSet, kLastElement + 1> sets;
};

// Out-of-line so the std::unique_ptr<AtomicScaleCache> member can be destroyed
// where AtomicScaleCache is a complete type.
PartialEngine::~PartialEngine() = default;

void PartialEngine::ensureAtomicScaleCache()
{
    // Message-thread only. Builds the 145-entry cache exactly once. Idempotent:
    // after construction the pointer is already populated, so prepare()'s call
    // is a no-op and never allocates.
    if (atomicScaleCache == nullptr)
    {
        atomicScaleCache = std::make_unique<AtomicScaleCache>();
        atomicScaleCache->sets = buildAtomicScaleResultSets();
    }
}

PartialEngine::PartialEngine()
{
    // Populate the atomic-scale cache up front, on the message thread, before the
    // engine is reachable by the audio thread. This guarantees every reader
    // (atomicResultFor / getScalePitch / renderVoices / UI getters) sees a fully
    // built cache and never triggers a lazy build (and thus never allocates) on
    // any audio-thread path.
    ensureAtomicScaleCache();

    for (auto& b : auroraBands) b.store(0.0f);
    initEnvelopeLuts();
    reset();
}

void PartialEngine::initEnvelopeLuts()
{
    for (int i = 0; i < HANN_LUT_SIZE; ++i)
    {
        const float t = (float) i / (float) (HANN_LUT_SIZE - 1);   // 0..1
        for (int shape = 0; shape < 4; ++shape)
            envelopeLuts[(size_t) shape][(size_t) i] = grainEnvelope(t, shape);
    }
}

void PartialEngine::prepare (double sr, int blockSize)
{
    // Build atomic scale/timbre caches away from the audio callback. Scale reduction
    // is musical/perceptual; raw timbre partials remain preserved in every result.
    // The cache is already built in the constructor, so this is a no-op in normal
    // operation; it stays here as the documented message-thread population point.
    ensureAtomicScaleCache();

    sampleRate = sr;
    delaySamples = juce::jlimit(1, MAX_DELAY_SAMPLES - 1,
                                (int) std::round(0.38 * sampleRate));
    reverb.setSampleRate(sampleRate);
    dryScratch.setSize(2, juce::jmax(512, blockSize * 2), false, false, true);
    reset();
}

void PartialEngine::reset()
{
    for (auto& v : voices)
    {
	        v.active = false; v.releasing = false;
	        v.seatRow = v.seatCol = -1;
	        v.targetMidi = 60;
	        v.targetFrequencyHz = midiToHz(60);
	        v.sample = nullptr;
        v.position = 0.0;
        v.playbackRate = 1.0;
        v.amp = v.targetAmp = 0.0f;
        v.lpCoef = v.targetLpCoef = 0.5f;
        v.lpZL = v.lpZR = 0.0f;
	        v.panL = v.panR = 0.707f;
	        v.xPos = 0.5f;
	        v.gainScale = 1.0f;
	        v.velocityGain = 1.0f;
            v.sourceMode = kEngineSampleLibrary;
            v.playbackMode = kSamplePlaybackDirect;
            v.elementIndex = kElementHelium;
            v.elementPhase.fill(0.0f);
        v.pitchLfoPhase = v.pitchLfoInc = v.pitchLfoDepth = 0.0f;
        v.ampLfoPhase   = v.ampLfoInc   = v.ampLfoDepth   = 0.0f;
        v.pitchLfoSin = 0.0f; v.pitchLfoCos = 1.0f;
        v.ampLfoSin   = 0.0f; v.ampLfoCos   = 1.0f;
        for (auto& g : v.grains)
        {
            g.active = false;
            g.reverse = false;
            g.position = 0.0;
            g.rateScale = 1.0;
            g.pan = 0.0f;
            g.panL = 1.0f;
            g.panR = 1.0f;
            g.age = 0;
            g.duration = 0;
        }
        v.spawnSampleCounter = 0;
        v.uiAmp.store(0.0f);
        v.uiX.store(0.5f);
        v.uiMidi.store(-1);
        v.uiScaleStep.store(-1);
    }
    for (auto& s : seats)
    {
        s.active.store(false);
        s.lastX.store(0.5f);
	        s.lastY.store(0.5f);
	        s.currentMidi.store(-1);
	        s.currentPitchKey.store(-1);
	        s.lastTriggerMs.store(0);
        for (auto& vi : s.voiceIdx) vi.store(-1);
    }
    for (auto& k : keyboardSlots)
    {
        k.active = false;
        k.step = -1;
        k.currentMidi = -1;
        k.currentPitchKey = -1;
        k.velocity = 0.0f;
        k.voiceIdx.fill(-1);
    }
    eventFifo.reset();
    midiEventFifo.reset();
    clearAllSeatsPending.store(false, std::memory_order_relaxed);
    retuneActiveSeatsPending.store(false, std::memory_order_relaxed);
    audioCallbackSampleClock = 0;
    activeVoiceCount.store(0);
    registeredSeatCount.store(0);
    adaptiveUnisonCount.store(3);

    // Every voice above was set inactive; keep the active-voice index list in sync.
    clearActiveVoiceList();

    delayBufL.fill(0.0f);
    delayBufR.fill(0.0f);
    delayWriteIdx = 0;
    reverb.reset();
    voiceSearchHint = 0;
    limGain = 1.0f;

    for (auto& b : auroraBands) b.store(0.0f);
}

int PartialEngine::seatIndex (int row, int col) noexcept
{
    if (row < 0 || row >= MAX_ROWS) return -1;
    if (col < 0 || col >= MAX_COLS) return -1;
    return row * MAX_COLS + col;
}

double PartialEngine::midiNoteToFrequencyHz (int midiNote) noexcept
{
    return midiToHz(juce::jlimit(0, 127, midiNote));
}

int PartialEngine::getVoiceLimit() const noexcept
{
    switch (juce::jlimit(0, 2, polyphonyMode.load(std::memory_order_relaxed)))
    {
        case 1:  return HIGH_VOICE_LIMIT;
        case 2:  return ULTRA_VOICE_LIMIT;
        default: return NORMAL_VOICE_LIMIT;
    }
}

juce::String PartialEngine::getPolyphonyModeName() const
{
    switch (juce::jlimit(0, 2, polyphonyMode.load(std::memory_order_relaxed)))
    {
        case 1:  return "High";
        case 2:  return "Ultra";
        default: return "Normal";
    }
}

int PartialEngine::computeAdaptiveUnisonCount (int registeredSeats, int voiceLimit) const noexcept
{
    const int seatCount = juce::jmax(1, registeredSeats);
    if (seatCount * 3 <= voiceLimit) return 3;
    if (seatCount * 2 <= voiceLimit) return 2;
    return 1;
}

int PartialEngine::countActiveVoices() const noexcept
{
    int active = 0;
    for (const auto& v : voices)
        if (v.active)
            ++active;
    return active;
}

int PartialEngine::getScaleTableSize() const noexcept
{
    const int mode = juce::jlimit(0, kTotalScaleModes - 1,
                                  scaleMode.load(std::memory_order_relaxed));
    const int atomicMode = atomicScaleMode.load(std::memory_order_relaxed);
    const int stepsPerOctave = isSpectralMode(mode)
        ? spectralLineCount(atomicScaleCache->sets, mode, atomicMode)
        : getScaleDef(mode).count;

    return juce::jmax(1, scaleOctaves.load(std::memory_order_relaxed)) * stepsPerOctave;
}

bool PartialEngine::isSpectralScale() const noexcept
{
    const int mode = juce::jlimit(0, kTotalScaleModes - 1,
                                  scaleMode.load(std::memory_order_relaxed));
    return isSpectralMode(mode);
}

int PartialEngine::getScaleStepsPerOctave() const noexcept
{
    const int mode = juce::jlimit(0, kTotalScaleModes - 1,
                                  scaleMode.load(std::memory_order_relaxed));
    return isSpectralMode(mode) ? spectralLineCount(atomicScaleCache->sets, mode, atomicScaleMode.load(std::memory_order_relaxed))
                                : getScaleDef(mode).count;
}

int PartialEngine::findNearestScaleStepForMidi (int midiNote) const noexcept
{
    const int total = getScaleTableSize();
    if (total <= 0)
        return -1;

    int bestStep = 0;
    int bestDistance = std::numeric_limits<int>::max();
    for (int step = 0; step < total; ++step)
    {
        const int stepMidi = getScaleMidi(step);
        if (stepMidi < 0)
            continue;

        const int distance = std::abs(stepMidi - midiNote);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            bestStep = step;
        }
    }

    return bestStep;
}

int PartialEngine::findKeyboardScaleStepForMidi (int midiNote) const noexcept
{
    const int total = getScaleTableSize();
    if (total <= 0)
        return -1;

    const int stepsPerOctave = juce::jlimit(1, total, getScaleStepsPerOctave());
    const int rootMidi = getScaleMidi(0);
    if (rootMidi < 0)
        return findNearestScaleStepForMidi(midiNote);

    auto foldedOffset = [] (int delta) noexcept
    {
        int offset = delta % 12;
        if (offset < 0)
            offset += 12;

        return offset == 0 && delta > 0 ? 12 : offset;
    };

    const int rootPc = ((rootMidi % 12) + 12) % 12;
    const int performanceBaseMidi = 24 + rootPc; // C1 plus selected root pitch class.
    const int inputOffset = foldedOffset(midiNote - performanceBaseMidi);
    if (inputOffset == 12 && stepsPerOctave > 1)
        return stepsPerOctave - 1;

    int bestStep = 0;
    int bestDistance = std::numeric_limits<int>::max();

    for (int step = 0; step < stepsPerOctave; ++step)
    {
        const int stepMidi = getScaleMidi(step);
        if (stepMidi < 0)
            continue;

        const int distance = std::abs(foldedOffset(stepMidi - rootMidi) - inputOffset);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            bestStep = step;
        }
    }

    return bestStep;
}

PartialEngine::PitchTarget PartialEngine::getScalePitch (int idx) const noexcept
{
    const int mode = juce::jlimit(0, kTotalScaleModes - 1,
                                  scaleMode.load(std::memory_order_relaxed));
    const int octaves = juce::jmax(1, scaleOctaves.load(std::memory_order_relaxed));

    if (isSpectralMode(mode))
    {
        const int atomicMode = atomicScaleMode.load(std::memory_order_relaxed);
        const auto& scale = atomicScaleResultForMode(atomicScaleCache->sets, mode, atomicMode);
        const int lineCount = (int) scale.scaleDegrees.size();
        if (lineCount <= 0)
            return {};

        const int total = octaves * lineCount;
        const int safeIdx = juce::jlimit(0, total - 1, idx);
        const int oct = safeIdx / lineCount;
        const int lineIdx = safeIdx % lineCount;
        const auto& degree = scale.scaleDegrees[(size_t) lineIdx];
        const double rootHz = midiToHz(scaleRootMidi.load(std::memory_order_relaxed));
        const double hz = rootHz * std::pow(2.0, degree.cents / 1200.0)
                        * std::pow(2.0, (double) oct);

        PitchTarget target;
        target.midi = hzToNearestMidi(hz);
        target.key = 10000 + mode * 1000 + safeIdx;
        target.frequencyHz = hz;
        // Partial Solo is for judging individual spectral pitches/partials, so
        // keep audition loudness even while the UI still shows line strength.
        target.velocityGain = spectralPartialSolo.load(std::memory_order_relaxed) != 0
            ? 1.0f
            : velocityToAudibleGain((float) degree.velocity);
        return target;
    }

    const auto& scale = getScaleDef(mode);
    const int total = octaves * scale.count;
    const int safeIdx = juce::jlimit(0, total - 1, idx);
    const int midi = scaleRootMidi.load(std::memory_order_relaxed)
                   + (safeIdx / scale.count) * 12
                   + scale.degrees[(size_t) (safeIdx % scale.count)];

    PitchTarget target;
    target.midi = midi;
    target.key = midi;
    target.frequencyHz = midiToHz(midi);
    target.velocityGain = 1.0f;
    return target;
}

PartialEngine::PitchTarget PartialEngine::xToPitch (float x) const noexcept
{
    // Scale mapping is independent of library size: the closest sample is
    // selected and pitched to the chosen MIDI note or exact spectral line.
    const int total = getScaleTableSize();
    int step = (int) (juce::jlimit(0.0f, 0.99999f, x) * (float) total);
    if (step < 0)      step = 0;
    if (step >= total) step = total - 1;
    return getScalePitch(step);
}

int PartialEngine::xToMidi (float x) const noexcept
{
    return xToPitch(x).midi;
}

int PartialEngine::loadSampleLibrary (const juce::File& dir)
{
    clearAllVoices();
    const int n = library.loadFromDirectory(dir);
    if (n > 0)
        retriggerActiveSeats();
    return n;
}

int PartialEngine::getScaleMidi (int idx) const noexcept
{
    if (idx < 0 || idx >= getScaleTableSize()) return -1;
    return getScalePitch(idx).midi;
}

double PartialEngine::getScaleFrequencyHz (int idx) const noexcept
{
    if (idx < 0 || idx >= getScaleTableSize()) return 0.0;
    return getScalePitch(idx).frequencyHz;
}

double PartialEngine::getScaleLineWavelengthNm (int idx) const noexcept
{
    const int mode = juce::jlimit(0, kTotalScaleModes - 1,
                                  scaleMode.load(std::memory_order_relaxed));
    if (! isSpectralMode(mode))
        return 0.0;

    const auto& scale = atomicScaleResultForMode(atomicScaleCache->sets, mode, atomicScaleMode.load(std::memory_order_relaxed));
    const int lineCount = (int) scale.scaleDegrees.size();
    if (lineCount <= 0)
        return 0.0;

    // Guard against negative / overflowing idx: a raw `idx % lineCount` can be
    // negative for negative idx and index out of bounds. Clamp to the valid range.
    const int lineIdx = juce::jlimit(0, lineCount - 1, idx % lineCount);
    return scale.scaleDegrees[(size_t) lineIdx].representativeWavelengthNm;
}

float PartialEngine::getScaleLineAmplitude (int idx) const noexcept
{
    const int mode = juce::jlimit(0, kTotalScaleModes - 1,
                                  scaleMode.load(std::memory_order_relaxed));
    if (! isSpectralMode(mode))
        return 0.0f;

    const auto& scale = atomicScaleResultForMode(atomicScaleCache->sets, mode, atomicScaleMode.load(std::memory_order_relaxed));
    const int lineCount = (int) scale.scaleDegrees.size();
    if (lineCount <= 0)
        return 0.0f;

    // Guard against negative / overflowing idx: a raw `idx % lineCount` can be
    // negative for negative idx and index out of bounds. Clamp to the valid range.
    const int lineIdx = juce::jlimit(0, lineCount - 1, idx % lineCount);
    return (float) scale.scaleDegrees[(size_t) lineIdx].velocity;
}

int PartialEngine::getScaleStepMidi (int step) const noexcept
{
    const int mode = juce::jlimit(0, kTotalScaleModes - 1,
                                  scaleMode.load(std::memory_order_relaxed));
    if (isSpectralMode(mode))
    {
        const int lineCount = spectralLineCount(atomicScaleCache->sets, mode, atomicScaleMode.load(std::memory_order_relaxed));
        if (lineCount <= 0)
            return scaleRootMidi.load(std::memory_order_relaxed);

        const int oct = floorDiv(step, lineCount);
        const int deg = step - oct * lineCount;
        return getScalePitch(oct * lineCount + juce::jlimit(0, lineCount - 1, deg)).midi;
    }

    const int rootMidi = scaleRootMidi.load(std::memory_order_relaxed);
    const auto& scale  = getScaleDef(mode);
    const int oct = floorDiv(step, scale.count);
    const int deg = step - oct * scale.count;
    return rootMidi + oct * 12 + scale.degrees[(size_t) deg];
}

int PartialEngine::getScaleDegreeOffsetSemis (int midi, int degreeOffset) const noexcept
{
    if (degreeOffset == 0)
        return 0;

    const int mode = juce::jlimit(0, kTotalScaleModes - 1,
                                  scaleMode.load(std::memory_order_relaxed));
    if (isSpectralMode(mode))
        return juce::jlimit(-24, 24, degreeOffset);

    const int rootMidi = scaleRootMidi.load(std::memory_order_relaxed);
    const auto& scale  = getScaleDef(mode);
    const int approxStep = floorDiv((midi - rootMidi) * scale.count, 12);

    int bestStep = approxStep;
    int bestDist = 999;
    for (int s = approxStep - scale.count * 2; s <= approxStep + scale.count * 2; ++s)
    {
        const int m = getScaleStepMidi(s);
        const int d = std::abs(m - midi);
        if (d < bestDist)
        {
            bestDist = d;
            bestStep = s;
        }
    }

    return getScaleStepMidi(bestStep + degreeOffset) - midi;
}

juce::String PartialEngine::getScaleRangeName() const
{
    static const char* names[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    auto midiName = [](int m) {
        const int oct = m / 12 - 1;
        return juce::String(names[((m % 12) + 12) % 12]) + juce::String(oct);
    };
    const int mode = juce::jlimit(0, kTotalScaleModes - 1,
                                  scaleMode.load(std::memory_order_relaxed));
    const int total = getScaleTableSize();
    if (isSpectralMode(mode))
    {
        const int atomicMode = atomicScaleMode.load(std::memory_order_relaxed);
        const auto& scale = atomicScaleResultForMode(atomicScaleCache->sets, mode, atomicMode);
        double lowHz = std::numeric_limits<double>::max();
        double highHz = 0.0;
        for (int i = 0; i < total; ++i)
        {
            const auto pitch = getScalePitch(i);
            lowHz = juce::jmin(lowHz, pitch.frequencyHz);
            highHz = juce::jmax(highHz, pitch.frequencyHz);
        }

        return juce::String((int) std::round(lowHz)) + "-"
             + juce::String((int) std::round(highHz)) + " Hz  "
             + juce::String(spectralName(mode))
             + " / " + juce::String(atomicScaleModeName(atomicMode))
             + "  (" + juce::String(scale.scaleDegrees.size()) + " degrees, "
             + juce::String(scale.rawLines.size()) + " raw)";
    }

    const int rootMidi = scaleRootMidi.load(std::memory_order_relaxed);
    const auto& scale  = getScaleDef(mode);
    const int lowMidi  = rootMidi;
    const int highMidi = rootMidi + ((total - 1) / scale.count) * 12
                       + scale.degrees[(size_t) ((total - 1) % scale.count)];
    return midiName(lowMidi) + " - " + midiName(highMidi)
         + "  " + juce::String(scale.name)
         + "  (" + juce::String(total) + " steps)";
}

juce::String PartialEngine::getScaleName() const
{
    const int mode = juce::jlimit(0, kTotalScaleModes - 1,
                                  scaleMode.load(std::memory_order_relaxed));
    return isSpectralMode(mode) ? juce::String(spectralName(mode))
                                : juce::String(getScaleDef(mode).name);
}

juce::String PartialEngine::getScaleOneOctaveDebugText() const
{
    const int mode = juce::jlimit(0, kTotalScaleModes - 1,
                                  scaleMode.load(std::memory_order_relaxed));
    const int rootMidi = scaleRootMidi.load(std::memory_order_relaxed);
    const double rootHz = midiToHz(rootMidi);
    const int steps = getScaleStepsPerOctave();

    juce::String s;
    s << getScaleName() << "\n";
    s << "root       : " << midiNoteName(rootMidi) << "  "
      << juce::String(rootHz, 2) << " Hz\n";
    s << "one octave : " << steps << " steps\n";
    s << "----------------------------------------\n";

    if (isSpectralMode(mode))
    {
        const int atomicMode = atomicScaleMode.load(std::memory_order_relaxed);
        const auto& scale = atomicScaleResultForMode(atomicScaleCache->sets, mode, atomicMode);

        s << "mode       : " << atomicScaleModeName(atomicMode) << "\n";
        s << "raw lines  : " << (int) scale.rawLines.size()
          << " preserved as timbre partials\n";
        s << "#  rep nm     cents    audio Hz  vel    cluster  spread  nearest\n";
        for (int i = 0; i < steps; ++i)
        {
            const auto& degree = scale.scaleDegrees[(size_t) i];
            const auto pitch = getScalePitch(i);
            const double cents = 1200.0 * std::log2(pitch.frequencyHz / rootHz);
            s << juce::String(i + 1).paddedLeft(' ', 2) << " "
              << juce::String(degree.representativeWavelengthNm, 3).paddedLeft(' ', 8) << "  "
              << juce::String(degree.cents, 1).paddedLeft(' ', 7) << "  "
              << juce::String(pitch.frequencyHz, 2).paddedLeft(' ', 8) << "  "
              << juce::String(degree.velocity, 3).paddedLeft(' ', 5) << "  "
              << juce::String(degree.clusterDensity).paddedLeft(' ', 7) << "  "
              << juce::String(degree.clusterSpreadCents, 1).paddedLeft(' ', 6) << "  "
              << midiNoteName(pitch.midi) << " "
              << (cents >= 0.0 ? "+" : "") << juce::String(cents, 1) << "ct\n";
        }
        return s;
    }

    s << "#  midi  note  audio Hz\n";
    for (int i = 0; i < steps; ++i)
    {
        const auto pitch = getScalePitch(i);
        s << juce::String(i + 1).paddedLeft(' ', 2) << " "
          << juce::String(pitch.midi).paddedLeft(' ', 5) << "  "
          << midiNoteName(pitch.midi).paddedRight(' ', 4) << "  "
          << juce::String(pitch.frequencyHz, 2) << " Hz\n";
    }

    return s;
}

juce::String PartialEngine::getSignatureModeName() const
{
    return getModeProfile(signatureMode.load()).name;
}

juce::String PartialEngine::getEngineSourceName() const
{
    return engineSource.load(std::memory_order_relaxed) == kEngineElementSynth
        ? "Element Spectral Synth"
        : "Sample Library";
}

juce::String PartialEngine::getSamplePlaybackModeName() const
{
    return samplePlaybackName(samplePlaybackMode.load(std::memory_order_relaxed));
}

juce::String PartialEngine::getSpectralElementName() const
{
    const int element = juce::jlimit(kElementHydrogen, kLastElement,
                                     spectralElement.load(std::memory_order_relaxed));
    return elementName(element);
}

double PartialEngine::getSpectralElementRootWavelengthNm() const noexcept
{
    const int element = juce::jlimit(kElementHydrogen, kLastElement,
                                     spectralElement.load(std::memory_order_relaxed));
    return elementReferenceWavelength(atomicScaleCache->sets, element);
}

int PartialEngine::getSpectralElementLineCount() const noexcept
{
    const int element = juce::jlimit(kElementHydrogen, kLastElement,
                                     spectralElement.load(std::memory_order_relaxed));
    return elementLineCount(atomicScaleCache->sets, element);
}

int PartialEngine::getActiveGrainCount() const noexcept
{
    int count = 0;
    for (const auto& v : voices)
    {
        if (! v.active || v.sourceMode != kEngineSampleLibrary || v.playbackMode != kSamplePlaybackGranular)
            continue;

        for (const auto& g : v.grains)
            if (g.active)
                ++count;
    }

    return count;
}

juce::String PartialEngine::getActiveSeatsSnapshot (int maxLines) const
{
    static const char* names[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };

    juce::String out;
    int lines = 0;
    for (int s = 0; s < MAX_SEATS && lines < maxLines; ++s)
    {
        if (! seats[(size_t) s].active.load(std::memory_order_relaxed)) continue;
        const int row  = s / MAX_COLS;
        const int col  = s % MAX_COLS;
        const float x  = seats[(size_t) s].lastX       .load(std::memory_order_relaxed);
        const float y  = seats[(size_t) s].lastY       .load(std::memory_order_relaxed);
        const int   m  = seats[(size_t) s].currentMidi .load(std::memory_order_relaxed);

        const char letter = (char) ('A' + juce::jlimit(0, 25, row));
        juce::String midiStr = (m >= 0)
            ? (juce::String(names[((m % 12) + 12) % 12]) + juce::String(m / 12 - 1))
            : juce::String("--");

        out << juce::String::charToString((juce::juce_wchar) letter)
            << juce::String(col).paddedRight(' ', 3)
            << "  X=" << juce::String(x, 3)
            << "   Y=" << juce::String(y, 3)
            << "   " << midiStr << "\n";
        ++lines;
    }
    if (out.isEmpty()) out = "(no active seats)";
    return out;
}

void PartialEngine::clearAllVoices()
{
    // Drop every voice cleanly so we can swap the sample library without
    // dangling pointers. Called from the message thread; the audio thread
    // will just see active=false and produce silence next block.
    for (auto& v : voices)
	    {
	        v.active = false; v.releasing = false;
	        v.seatRow = v.seatCol = -1;
	        v.targetMidi = 60;
	        v.targetFrequencyHz = midiToHz(60);
	        v.sample = nullptr;
	        v.amp = v.targetAmp = 0.0f;
	        v.velocityGain = 1.0f;
            v.sourceMode = kEngineSampleLibrary;
            v.playbackMode = kSamplePlaybackDirect;
            v.elementIndex = kElementHelium;
            v.elementPhase.fill(0.0f);
        for (auto& g : v.grains)
        {
            g.active = false;
            g.reverse = false;
            g.age = 0;
            g.duration = 0;
            g.rateScale = 1.0;
            g.pan = 0.0f;
            g.panL = 1.0f;
            g.panR = 1.0f;
        }
        v.spawnSampleCounter = 0;
        v.uiAmp.store(0.0f);
        v.uiMidi.store(-1);
        v.uiScaleStep.store(-1);
        v.uiSeatRow.store(-1);
        v.uiSeatCol.store(-1);
    }
    for (auto& s : seats)
    {
	        for (auto& vi : s.voiceIdx) vi.store(-1);
	        s.currentMidi.store(-1);
	        s.currentPitchKey.store(-1);
    }
    for (auto& k : keyboardSlots)
    {
        k.active = false;
        k.step = -1;
        k.currentMidi = -1;
        k.currentPitchKey = -1;
        k.velocity = 0.0f;
        k.voiceIdx.fill(-1);
    }
    activeVoiceCount.store(0);
    // Every voice above was set inactive; keep the active-voice index list in sync.
    clearActiveVoiceList();
}

void PartialEngine::clearAllSeats()
{
    discardPendingEvents();
    enqueueMidiEvent({ MidiSourceEvent::AllNotesOff });
    clearAllSeatsPending.store(false, std::memory_order_relaxed);
    retuneActiveSeatsPending.store(false, std::memory_order_relaxed);
    clearAllVoices();
    for (auto& s : seats)
    {
	        s.active.store(false, std::memory_order_relaxed);
	        s.currentMidi.store(-1, std::memory_order_relaxed);
	        s.currentPitchKey.store(-1, std::memory_order_relaxed);
	        for (auto& vi : s.voiceIdx) vi.store(-1, std::memory_order_relaxed);
    }
    registeredSeatCount.store(0, std::memory_order_relaxed);
    adaptiveUnisonCount.store(3, std::memory_order_relaxed);

    delayBufL.fill(0.0f);
    delayBufR.fill(0.0f);
    delayWriteIdx = 0;
    reverb.reset();
    limGain = 1.0f;
    for (auto& b : auroraBands) b.store(0.0f, std::memory_order_relaxed);
}

void PartialEngine::requestClearAllSeats()
{
    clearAllSeatsPending.store(true, std::memory_order_relaxed);
}

void PartialEngine::requestRetuneActiveSeats()
{
    retuneActiveSeatsPending.store(true, std::memory_order_relaxed);
}

void PartialEngine::processPendingCommands()
{
    if (clearAllSeatsPending.exchange(false, std::memory_order_relaxed))
    {
        clearAllSeats();
        return;
    }

    if (retuneActiveSeatsPending.exchange(false, std::memory_order_relaxed))
    {
        enqueueMidiEvent({ MidiSourceEvent::AllNotesOff });
        clearAllVoices();
        retuneActiveSeatsNow();
    }
}

void PartialEngine::retriggerActiveSeats()
{
    for (int s = 0; s < MAX_SEATS; ++s)
    {
        if (! seats[(size_t) s].active.load(std::memory_order_relaxed))
            continue;

        enqueueEvent({ VoiceEvent::On,
                       (juce::int16) (s / MAX_COLS),
                       (juce::int16) (s % MAX_COLS),
                       0.0f });
    }
}

void PartialEngine::discardPendingEvents()
{
    const int available = eventFifo.getNumReady();
    if (available <= 0)
        return;

    int s1, sz1, s2, sz2;
    eventFifo.prepareToRead(available, s1, sz1, s2, sz2);
    eventFifo.finishedRead(sz1 + sz2);
}

void PartialEngine::retuneActiveSeatsNow()
{
    for (int s = 0; s < MAX_SEATS; ++s)
    {
        if (! seats[(size_t) s].active.load(std::memory_order_relaxed))
            continue;

        maybeTrigger(s / MAX_COLS, s % MAX_COLS, s,
                     seats[(size_t) s].lastX.load(std::memory_order_relaxed),
                     true);
    }
}

float PartialEngine::getVoiceAmp (int i) const noexcept
{
    if (i < 0 || i >= MAX_VOICES) return 0.0f;
    return voices[(size_t) i].uiAmp.load(std::memory_order_relaxed);
}

int PartialEngine::getVoiceMidi (int i) const noexcept
{
    if (i < 0 || i >= MAX_VOICES) return -1;
    return voices[(size_t) i].uiMidi.load(std::memory_order_relaxed);
}

int PartialEngine::getVoiceScaleStep (int i) const noexcept
{
    if (i < 0 || i >= MAX_VOICES) return -1;
    return voices[(size_t) i].uiScaleStep.load(std::memory_order_relaxed);
}

int PartialEngine::getVoiceSeatRow (int i) const noexcept
{
    if (i < 0 || i >= MAX_VOICES) return -1;
    return voices[(size_t) i].uiSeatRow.load(std::memory_order_relaxed);
}

int PartialEngine::getVoiceSeatCol (int i) const noexcept
{
    if (i < 0 || i >= MAX_VOICES) return -1;
    return voices[(size_t) i].uiSeatCol.load(std::memory_order_relaxed);
}

bool PartialEngine::isSeatActive (int row, int col) const noexcept
{
    const int idx = seatIndex(row, col);
    return idx >= 0 && seats[(size_t) idx].active.load(std::memory_order_relaxed);
}

float PartialEngine::getSeatX (int row, int col) const noexcept
{
    const int idx = seatIndex(row, col);
    return idx < 0 ? 0.0f : seats[(size_t) idx].lastX.load(std::memory_order_relaxed);
}

float PartialEngine::getSeatY (int row, int col) const noexcept
{
    const int idx = seatIndex(row, col);
    return idx < 0 ? 0.0f : seats[(size_t) idx].lastY.load(std::memory_order_relaxed);
}

juce::String PartialEngine::getDominantSampleName() const
{
    if (engineSource.load(std::memory_order_relaxed) == kEngineElementSynth)
    {
        const double rootNm = getSpectralElementRootWavelengthNm();
        return getSpectralElementName() + " "
             + juce::String(rootNm, 3) + " nm";
    }

    float bestAmp = 0.0f;
    int   bestMidi = -1;
    for (int i = 0; i < MAX_VOICES; ++i)
    {
        const float a = voices[(size_t) i].uiAmp.load(std::memory_order_relaxed);
        if (a > bestAmp) { bestAmp = a; bestMidi = voices[(size_t) i].uiMidi.load(); }
    }
    if (bestMidi < 0) return library.numSamples() > 0 ? "(silent)" : "(no samples)";

    static const char* names[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    const int oct = bestMidi / 12 - 1;
    return juce::String(names[bestMidi % 12]) + juce::String(oct);
}

// ---------- OSC thread API ----------

void PartialEngine::setX (int row, int col, float xNorm)
{
    const int idx = seatIndex(row, col);
    if (idx < 0) return;
    seats[(size_t) idx].lastX.store(xNorm, std::memory_order_relaxed);
    enqueueEvent({ VoiceEvent::XChange, (juce::int16) row, (juce::int16) col, xNorm });
}

void PartialEngine::setY (int row, int col, float yNorm)
{
    const int idx = seatIndex(row, col);
    if (idx < 0) return;
    seats[(size_t) idx].lastY.store(yNorm, std::memory_order_relaxed);
    enqueueEvent({ VoiceEvent::YChange, (juce::int16) row, (juce::int16) col, yNorm });
}

void PartialEngine::setOn (int row, int col, bool on)
{
    const int idx = seatIndex(row, col);
    if (idx < 0) return;
    const bool wasActive = seats[(size_t) idx].active.exchange(on, std::memory_order_relaxed);
    if (wasActive != on)
    {
        const int delta = on ? 1 : -1;
        const int updated = registeredSeatCount.fetch_add(delta, std::memory_order_relaxed) + delta;
        if (updated < 0)
            registeredSeatCount.store(0, std::memory_order_relaxed);
    }
    enqueueEvent({ on ? VoiceEvent::On : VoiceEvent::Off,
                   (juce::int16) row, (juce::int16) col, 0.0f });
}

void PartialEngine::setKeyboardStep (int slot, int scaleStep, float velocity, bool on)
{
    if (slot < 0 || slot >= MAX_KEYBOARD_SLOTS)
        return;

    enqueueEvent({ on ? VoiceEvent::KeyboardOn : VoiceEvent::KeyboardOff,
                   (juce::int16) slot,
                   (juce::int16) scaleStep,
                   juce::jlimit(0.0f, 1.0f, velocity) });
}

void PartialEngine::processKeyboardStepRealtime (int slot, int scaleStep, float velocity, bool on)
{
    if (slot < 0 || slot >= MAX_KEYBOARD_SLOTS)
        return;

    if (on)
        keyboardOn(slot, scaleStep, juce::jlimit(0.0f, 1.0f, velocity));
    else
        keyboardOff(slot);
}

void PartialEngine::processKeyboardPitchRealtime (int slot, int midiNote, double frequencyHz, float velocity, bool on)
{
    if (slot < 0 || slot >= MAX_KEYBOARD_SLOTS)
        return;

    if (! on)
    {
        keyboardOff(slot);
        return;
    }

    const int safeMidi = juce::jlimit(0, 127, midiNote);
    PitchTarget target;
    target.midi = safeMidi;
    target.key = safeMidi;
    target.frequencyHz = juce::jmax(1.0, frequencyHz);
    target.velocityGain = 1.0f;

    const float x = (float) safeMidi / 127.0f;
    const int visualStep = findNearestScaleStepForMidi(safeMidi);
    keyboardPitchOn(slot, target, visualStep, x, juce::jlimit(0.0f, 1.0f, velocity));
}

void PartialEngine::releaseAllKeyboardNotes()
{
    for (int slot = 0; slot < MAX_KEYBOARD_SLOTS; ++slot)
        setKeyboardStep(slot, 0, 0.0f, false);
}

void PartialEngine::enqueueEvent (const VoiceEvent& e)
{
    const juce::ScopedLock lock(eventWriteLock);
    int s1, sz1, s2, sz2;
    eventFifo.prepareToWrite(1, s1, sz1, s2, sz2);
    if (sz1 > 0)      { eventBuffer[(size_t) s1] = e; eventFifo.finishedWrite(1); }
    else if (sz2 > 0) { eventBuffer[(size_t) s2] = e; eventFifo.finishedWrite(1); }
}

void PartialEngine::enqueueMidiEvent (const MidiSourceEvent& e) noexcept
{
    int s1, sz1, s2, sz2;
    midiEventFifo.prepareToWrite(1, s1, sz1, s2, sz2);
    if (sz1 > 0)      { midiEventBuffer[(size_t) s1] = e; midiEventFifo.finishedWrite(1); }
    else if (sz2 > 0) { midiEventBuffer[(size_t) s2] = e; midiEventFifo.finishedWrite(1); }
}

int PartialEngine::drainMidiSourceEvents (MidiSourceEvent* dest, int maxEvents) noexcept
{
    if (dest == nullptr || maxEvents <= 0)
        return 0;

    const int avail = juce::jmin(maxEvents, midiEventFifo.getNumReady());
    if (avail <= 0)
        return 0;

    int s1, sz1, s2, sz2;
    midiEventFifo.prepareToRead(avail, s1, sz1, s2, sz2);

    int written = 0;
    for (int i = 0; i < sz1; ++i)
        dest[written++] = midiEventBuffer[(size_t) (s1 + i)];
    for (int i = 0; i < sz2; ++i)
        dest[written++] = midiEventBuffer[(size_t) (s2 + i)];

    midiEventFifo.finishedRead(sz1 + sz2);
    return written;
}

void PartialEngine::enqueueSeatMidiNoteOff (int row, int col, int sourceId) noexcept
{
    enqueueMidiEvent({ MidiSourceEvent::NoteOff,
                       (juce::int16) row,
                       (juce::int16) col,
                       (juce::int16) sourceId });
}

void PartialEngine::enqueueSeatMidiExpression (int row, int col, int sourceId, int midi,
                                               double frequencyHz, float x, float y) noexcept
{
    enqueueMidiEvent({ MidiSourceEvent::Expression,
                       (juce::int16) row,
                       (juce::int16) col,
                       (juce::int16) sourceId,
                       (juce::int16) midi,
                       frequencyHz,
                       y,
                       x,
                       y });
}

// ---------- audio thread ----------

void PartialEngine::drainEvents()
{
    const int avail = eventFifo.getNumReady();
    if (avail <= 0) return;
    int s1, sz1, s2, sz2;
    eventFifo.prepareToRead(avail, s1, sz1, s2, sz2);
    for (int i = 0; i < sz1; ++i) handleEvent(eventBuffer[(size_t)(s1 + i)]);
    for (int i = 0; i < sz2; ++i) handleEvent(eventBuffer[(size_t)(s2 + i)]);
    eventFifo.finishedRead(sz1 + sz2);
}

void PartialEngine::handleEvent (const VoiceEvent& e)
{
    if ((VoiceEvent::Type) e.type == VoiceEvent::KeyboardOn)
    {
        keyboardOn((int) e.row, (int) e.col, e.value);
        return;
    }

    if ((VoiceEvent::Type) e.type == VoiceEvent::KeyboardOff)
    {
        keyboardOff((int) e.row);
        return;
    }

    const int sIdx = seatIndex(e.row, e.col);
    if (sIdx < 0) return;
    auto& s = seats[(size_t) sIdx];

    switch ((VoiceEvent::Type) e.type)
    {
        case VoiceEvent::On:
        {
            maybeTrigger(e.row, e.col, sIdx, s.lastX.load(), true);
            break;
        }
        case VoiceEvent::Off:
        {
            if (s.currentMidi.load(std::memory_order_relaxed) >= 0)
                enqueueSeatMidiNoteOff(e.row, e.col, sIdx);

            for (auto& v : voices)
            {
                if (v.active && v.seatRow == e.row && v.seatCol == e.col)
                {
                    v.releasing  = true;
                    v.targetAmp  = 0.0f;
                }
	            }
	            for (auto& vi : s.voiceIdx) vi.store(-1, std::memory_order_relaxed);
	            s.currentMidi.store(-1, std::memory_order_relaxed);
	            s.currentPitchKey.store(-1, std::memory_order_relaxed);
	            break;
	        }
        case VoiceEvent::XChange:
        {
            if (s.currentMidi.load(std::memory_order_relaxed) >= 0)
                maybeTrigger(e.row, e.col, sIdx, e.value, false);

            // brightness sweep on every unison voice belonging to this seat
            for (auto& vi : s.voiceIdx)
            {
                const int vIdx = vi.load(std::memory_order_relaxed);
                if (vIdx < 0) continue;
                auto& v = voices[(size_t) vIdx];
                v.xPos = e.value;
                v.targetLpCoef = computeFilterCoefFromX(e.value) * v.lpScale;
                v.uiX.store(e.value, std::memory_order_relaxed);
            }

            if (s.currentMidi.load(std::memory_order_relaxed) >= 0)
                enqueueSeatMidiExpression(e.row, e.col, sIdx,
                    s.currentMidi.load(std::memory_order_relaxed),
                    xToPitch(e.value).frequencyHz,
                    e.value,
                    s.lastY.load(std::memory_order_relaxed));
            break;
        }
        case VoiceEvent::YChange:
        {
            const float energy = juce::jlimit(0.0f, 1.0f, energyMacro.load());
            const float yNorm = juce::jlimit(0.0f, 1.0f, e.value);
            const auto& modeProfile = getModeProfile(signatureMode.load());
            const float yScaled = yNorm * layerMix.load()
                                * (0.70f + energy * 0.65f)
                                * (0.92f + modeProfile.tapeBoost * 0.6f);
            for (auto& vi : s.voiceIdx)
            {
	                const int vIdx = vi.load(std::memory_order_relaxed);
	                if (vIdx < 0) continue;
	                auto& v = voices[(size_t) vIdx];
	                if (! v.releasing) v.targetAmp = yScaled * v.velocityGain;
	            }

            if (s.currentMidi.load(std::memory_order_relaxed) >= 0)
                enqueueSeatMidiExpression(e.row, e.col, sIdx,
                    s.currentMidi.load(std::memory_order_relaxed),
                    xToPitch(s.lastX.load(std::memory_order_relaxed)).frequencyHz,
                    s.lastX.load(std::memory_order_relaxed),
                    yNorm);
	            break;
	        }
        case VoiceEvent::KeyboardOn:
        case VoiceEvent::KeyboardOff:
            break;
    }
}

void PartialEngine::keyboardOn (int slot, int scaleStep, float velocity)
{
    if (slot < 0 || slot >= MAX_KEYBOARD_SLOTS)
        return;

    const int total = getScaleTableSize();
    if (total <= 0)
        return;

    const int safeStep = juce::jlimit(0, total - 1, scaleStep);
    const auto target = getScalePitch(safeStep);
    const float x = total > 1 ? (float) safeStep / (float) (total - 1) : 0.5f;
    const float y = juce::jlimit(0.0f, 1.0f, velocity);
    keyboardPitchOn(slot, target, safeStep, x, y);
}

void PartialEngine::keyboardPitchOn (int slot, const PitchTarget& target, int scaleStep, float x, float velocity)
{
    if (slot < 0 || slot >= MAX_KEYBOARD_SLOTS)
        return;

    auto& state = keyboardSlots[(size_t) slot];
    if (state.currentMidi >= 0)
        enqueueSeatMidiNoteOff(-1, slot, keyboardSourceId(slot));

    for (auto& vi : state.voiceIdx)
    {
        if (vi >= 0)
            freeVoice(vi);
        vi = -1;
    }

    const float y = juce::jlimit(0.0f, 1.0f, velocity);
    const int voiceLimit = getVoiceLimit();
    const int activeSources = juce::jmax(1, registeredSeatCount.load(std::memory_order_relaxed) + 1);
    const int unisonCount = computeAdaptiveUnisonCount(activeSources, voiceLimit);
    adaptiveUnisonCount.store(unisonCount, std::memory_order_relaxed);

    const int panCol = juce::jlimit(0, MAX_COLS - 1,
                                    (int) std::round(juce::jlimit(0.0f, 1.0f, x) * (float) (MAX_COLS - 1)));
    const int sideSign = (slot & 1) == 0 ? 1 : -1;
    const float centerGain = unisonCount == 1 ? 0.86f : 0.55f;
    const float sideGain   = unisonCount == 2 ? 0.50f : 0.42f;

    state.voiceIdx[0] = allocateVoice(-1, panCol, target.midi, target.frequencyHz, target.velocityGain,
                                       0.0f, centerGain, 0.0f, x, y, scaleStep);
    state.voiceIdx[1] = unisonCount >= 2
        ? allocateVoice(-1, panCol, target.midi, target.frequencyHz, target.velocityGain,
                        (float) sideSign * 7.0f, sideGain, (float) sideSign * 0.18f, x, y, scaleStep)
        : -1;
    state.voiceIdx[2] = unisonCount >= 3
        ? allocateVoice(-1, panCol, target.midi, target.frequencyHz, target.velocityGain,
                        (float) -sideSign * 7.0f, 0.42f, (float) -sideSign * 0.18f, x, y, scaleStep)
        : -1;

    state.active = true;
    state.step = scaleStep;
    state.currentMidi = target.midi;
    state.currentPitchKey = target.key;
    state.velocity = y;

    enqueueMidiEvent({ MidiSourceEvent::NoteOn,
                       (juce::int16) -1,
                       (juce::int16) slot,
                       (juce::int16) keyboardSourceId(slot),
                       (juce::int16) target.midi,
                       target.frequencyHz,
                       y,
                       x,
                       y });
}

void PartialEngine::keyboardOff (int slot)
{
    if (slot < 0 || slot >= MAX_KEYBOARD_SLOTS)
        return;

    auto& state = keyboardSlots[(size_t) slot];
    if (state.currentMidi >= 0)
        enqueueSeatMidiNoteOff(-1, slot, keyboardSourceId(slot));

    for (auto& vi : state.voiceIdx)
    {
        const int vIdx = vi;
        if (vIdx >= 0 && vIdx < MAX_VOICES)
        {
            auto& v = voices[(size_t) vIdx];
            if (v.active)
            {
                v.releasing = true;
                v.targetAmp = 0.0f;
            }
        }
        vi = -1;
    }

    state.active = false;
    state.step = -1;
    state.currentMidi = -1;
    state.currentPitchKey = -1;
    state.velocity = 0.0f;
}

void PartialEngine::maybeTrigger (int row, int col, int sIdx, float x, bool forceTrigger)
{
    auto& s = seats[(size_t) sIdx];
    if (! s.active.load(std::memory_order_relaxed))
        return;

    const int totalSteps = getScaleTableSize();
    const int scaleStep = totalSteps > 0
        ? juce::jlimit(0, totalSteps - 1,
                       (int) (juce::jlimit(0.0f, 0.99999f, x) * (float) totalSteps))
        : -1;
    const auto target = xToPitch(x);
    const int newMidi = target.midi;
    const int curPitchKey = s.currentPitchKey.load(std::memory_order_relaxed);

    const uint32_t now  = sampleRate > 0.0
        ? (uint32_t) ((double) audioCallbackSampleClock * 1000.0 / sampleRate)
        : 0;
    const uint32_t last = s.lastTriggerMs.load(std::memory_order_relaxed);
    const float energy  = juce::jlimit(0.0f, 1.0f, energyMacro.load());
    const uint32_t hyst = (uint32_t) juce::jmax(8.0f, minTriggerMs.load() * (1.15f - energy * 0.55f));

    if (target.key == curPitchKey && ! forceTrigger) return;
    if (! forceTrigger && (now - last) < hyst) return;

    // X movement means "replace this participant's note", not layer it.
    // Hard-freeing the previous unison group keeps Random Movement bounded.
    if (s.currentMidi.load(std::memory_order_relaxed) >= 0)
        enqueueSeatMidiNoteOff(row, col, sIdx);

    for (auto& vi : s.voiceIdx)
    {
        const int oldIdx = vi.load(std::memory_order_relaxed);
        if (oldIdx >= 0)
            freeVoice(oldIdx);
        vi.store(-1, std::memory_order_relaxed);
    }

    const float y = s.lastY.load(std::memory_order_relaxed);
    const int voiceLimit = getVoiceLimit();
    const int registered = juce::jmax(1, registeredSeatCount.load(std::memory_order_relaxed));
    const int unisonCount = computeAdaptiveUnisonCount(registered, voiceLimit);
    adaptiveUnisonCount.store(unisonCount, std::memory_order_relaxed);

    const int sideSign = ((row + col) & 1) == 0 ? 1 : -1;
    const float centerGain = unisonCount == 1 ? 0.86f : 0.55f;
    const float sideGain   = unisonCount == 2 ? 0.50f : 0.42f;

    const int v0 = allocateVoice(row, col, newMidi, target.frequencyHz, target.velocityGain,
                                 0.0f, centerGain, 0.0f, x, y, scaleStep);
    const int v1 = unisonCount >= 2
                 ? allocateVoice(row, col, newMidi, target.frequencyHz, target.velocityGain,
                                 (float) sideSign * 7.0f, sideGain,
                                 (float) sideSign * 0.18f, x, y, scaleStep)
                 : -1;
    const int v2 = unisonCount >= 3
                 ? allocateVoice(row, col, newMidi, target.frequencyHz, target.velocityGain,
                                 (float) -sideSign * 7.0f, 0.42f,
                                 (float) -sideSign * 0.18f, x, y, scaleStep)
                 : -1;

    s.voiceIdx[0].store(v0, std::memory_order_relaxed);
    s.voiceIdx[1].store(v1, std::memory_order_relaxed);
    s.voiceIdx[2].store(v2, std::memory_order_relaxed);
    s.currentMidi  .store(newMidi, std::memory_order_relaxed);
    s.currentPitchKey.store(target.key, std::memory_order_relaxed);
    s.lastTriggerMs.store(now,     std::memory_order_relaxed);

    enqueueMidiEvent({ MidiSourceEvent::NoteOn,
                       (juce::int16) row,
                       (juce::int16) col,
                       (juce::int16) sIdx,
                       (juce::int16) newMidi,
                       target.frequencyHz,
                       y,
                       x,
                       y });
}

// ---- active-voice index list helpers (audio-thread only) -------------------
// The list mirrors the voices[].active flags exactly: activeVoiceSlot[i] >= 0
// iff voices[i].active. It is kept sorted ascending so renderVoices() visits
// live voices in the same order the old full 0..MAX_VOICES sweep did, keeping
// audio output byte-for-byte identical. All operations touch only fixed-size
// arrays (no allocation, no locking).

void PartialEngine::clearActiveVoiceList() noexcept
{
    // Reset the whole slot map (not just live entries) so the list is correct
    // regardless of prior state. Only invoked from reset()/clearAllVoices(),
    // never from the per-block hot path.
    activeVoiceSlot.fill(-1);
    activeVoiceCountRT = 0;
}

void PartialEngine::addActiveVoice (int idx) noexcept
{
    if (idx < 0 || idx >= MAX_VOICES)
        return;
    if (activeVoiceSlot[(size_t) idx] >= 0)
        return; // already present

    // Insert keeping the list sorted ascending.
    int p = activeVoiceCountRT;
    while (p > 0 && activeVoiceList[(size_t) (p - 1)] > idx)
    {
        const int moved = activeVoiceList[(size_t) (p - 1)];
        activeVoiceList[(size_t) p] = moved;
        activeVoiceSlot[(size_t) moved] = p;
        --p;
    }
    activeVoiceList[(size_t) p] = idx;
    activeVoiceSlot[(size_t) idx] = p;
    ++activeVoiceCountRT;
}

void PartialEngine::removeActiveVoice (int idx) noexcept
{
    if (idx < 0 || idx >= MAX_VOICES)
        return;
    const int p = activeVoiceSlot[(size_t) idx];
    if (p < 0)
        return; // not present

    // Shift the (sorted) tail left to close the gap.
    for (int j = p; j < activeVoiceCountRT - 1; ++j)
    {
        const int moved = activeVoiceList[(size_t) (j + 1)];
        activeVoiceList[(size_t) j] = moved;
        activeVoiceSlot[(size_t) moved] = j;
    }
    activeVoiceSlot[(size_t) idx] = -1;
    --activeVoiceCountRT;
}

int PartialEngine::allocateVoice (int row, int col, int midi,
                                  double frequencyHz, float velocityGain,
                                  float detuneCents, float gainScale, float panOffset,
                                  float x, float y, int scaleStep)
{
    const int source = juce::jlimit(kEngineSampleLibrary, kEngineElementSynth,
                                    engineSource.load(std::memory_order_relaxed));
    if (source == kEngineSampleLibrary && library.numSamples() == 0)
        return -1;

    const int voiceLimit = juce::jlimit(1, MAX_VOICES, getVoiceLimit());
    voiceSearchHint %= voiceLimit;

    int idx = -1;
    for (int n = 0; n < voiceLimit; ++n)
    {
        const int i = (voiceSearchHint + n) % voiceLimit;
        if (! voices[(size_t) i].active) { idx = i; break; }
    }
    if (idx < 0)
    {
        float worst = 1.0e9f;
        idx = 0;
        for (int i = 0; i < voiceLimit; ++i)
        {
            const float a = voices[(size_t) i].amp;
            if (a < worst) { worst = a; idx = i; }
        }
        freeVoice(idx);
    }
    voiceSearchHint = (idx + 1) % voiceLimit;

    auto& v = voices[(size_t) idx];
    v.active    = true;
    addActiveVoice(idx);
    v.releasing = false;
    v.seatRow   = row;
    v.seatCol   = col;
    v.targetMidi = midi;
    v.targetFrequencyHz = juce::jmax(1.0, frequencyHz);
    v.xPos       = x;
    v.gainScale  = gainScale;
    v.velocityGain = juce::jlimit(0.0f, 1.0f, velocityGain);
    v.sourceMode = source;
    v.playbackMode = juce::jlimit(kSamplePlaybackDirect, kSamplePlaybackGranular,
                                  samplePlaybackMode.load(std::memory_order_relaxed));
    v.elementIndex = juce::jlimit(kElementHydrogen, kLastElement,
                                  spectralElement.load(std::memory_order_relaxed));
    for (auto& phase : v.elementPhase)
        phase = rngVoice.nextFloat() * kTwoPi;

    v.amp       = 0.0f;
    const float energy = juce::jlimit(0.0f, 1.0f, energyMacro.load());
    const float yNorm = juce::jlimit(0.0f, 1.0f, y);
    const auto& modeProfile = getModeProfile(signatureMode.load());
    v.targetAmp = yNorm * layerMix.load()
                * (0.70f + energy * 0.65f)
                * (0.92f + modeProfile.tapeBoost * 0.6f)
                * v.velocityGain;

    v.sample = source == kEngineSampleLibrary ? library.getSampleForMidi(midi) : nullptr;
    if (v.sample != nullptr)
    {
        const double detuneRatio = std::pow(2.0, (double) detuneCents / 1200.0);
        const double pitchRatio = (v.targetFrequencyHz * detuneRatio)
                                / juce::jmax(1.0, v.sample->rootFreq);
        v.playbackRate = pitchRatio * (v.sample->fileSampleRate / sampleRate);

        const int len = juce::jmax(1, v.sample->trimmedLength);
        v.position = v.playbackMode == kSamplePlaybackGranular
            ? (double) rngVoice.nextFloat() * (double) len * 0.7
            : 0.0;
    }
    else
    {
        v.playbackRate = 1.0;
        v.position     = 0.0;
    }

    // ---- Spatial mapping (rows A..F front/back, 30 seats per row) ----
    const int spatialRow = row < 0 ? MAX_ROWS / 2 : row;
    const int spatialCol = juce::jlimit(0, MAX_COLS - 1, col);
    const float colNorm0 = ((float) spatialCol / (float) (MAX_COLS - 1)) * 2.0f - 1.0f;
    const float colNorm  = juce::jlimit(-1.0f, 1.0f, colNorm0 + panOffset);
    const float depth    = juce::jlimit(0.0f, 1.0f, (float) spatialRow / (float) (MAX_ROWS - 1));
    const float depthGain    = 1.0f - depth * 0.40f;
    const float depthLpScale = 1.0f - depth * 0.55f;
    v.lpScale = depthLpScale;

    v.lpCoef = v.targetLpCoef = computeFilterCoefFromX(x) * v.lpScale;
    v.lpZL = v.lpZR = 0.0f;

    const float theta = (colNorm + 1.0f) * (kPi * 0.25f);
    v.panL = std::cos(theta) * depthGain;
    v.panR = std::sin(theta) * depthGain;

    // ---- Slow per-voice LFOs (random phase + rate -> independent breathing) ----
    const float sr = (float) sampleRate;
    v.pitchLfoPhase = rngVoice.nextFloat() * kTwoPi;
    v.pitchLfoSin   = std::sin(v.pitchLfoPhase);
    v.pitchLfoCos   = std::cos(v.pitchLfoPhase);
    v.pitchLfoInc   = kTwoPi * (0.07f + rngVoice.nextFloat() * 0.20f) / sr;   // 0.07..0.27 Hz
    const float motion = juce::jlimit(0.0f, 1.0f, motionMacro.load() * modeProfile.motionMul);
    v.pitchLfoDepth = 0.025f + motion * 0.075f + modeProfile.pitchSpreadOffset * 0.006f; // semitone units
    v.ampLfoPhase   = rngVoice.nextFloat() * kTwoPi;
    v.ampLfoSin     = std::sin(v.ampLfoPhase);
    v.ampLfoCos     = std::cos(v.ampLfoPhase);
    v.ampLfoInc     = kTwoPi * (0.05f + rngVoice.nextFloat() * 0.15f) / sr;   // 0.05..0.20 Hz
    v.ampLfoDepth   = 0.06f + motion * 0.16f;

    for (auto& g : v.grains)
    {
        g.active = false;
        g.reverse = false;
        g.age = 0;
        g.duration = 0;
        g.position = 0.0;
        g.rateScale = 1.0;
        g.pan = 0.0f;
        g.panL = 1.0f;
        g.panR = 1.0f;
    }
    v.spawnSampleCounter = 0;

    v.uiX      .store(x,    std::memory_order_relaxed);
    v.uiMidi   .store(midi, std::memory_order_relaxed);
    v.uiScaleStep.store(scaleStep, std::memory_order_relaxed);
    v.uiSeatRow.store(row,  std::memory_order_relaxed);
    v.uiSeatCol.store(col,  std::memory_order_relaxed);
    return idx;
}

void PartialEngine::freeVoice (int idx)
{
    if (idx < 0 || idx >= MAX_VOICES) return;
    auto& v = voices[(size_t) idx];
    if (v.seatRow >= 0 && v.seatCol >= 0)
    {
        const int sIdx = seatIndex(v.seatRow, v.seatCol);
        if (sIdx >= 0)
        {
            auto& s = seats[(size_t) sIdx];
            for (auto& vi : s.voiceIdx)
                if (vi.load(std::memory_order_relaxed) == idx)
                    vi.store(-1, std::memory_order_relaxed);
        }
    }
	    v.active = false; v.releasing = false;
	    removeActiveVoice(idx);
	    v.seatRow = v.seatCol = -1;
	    v.targetMidi = 60;
	    v.targetFrequencyHz = midiToHz(60);
	    v.sample = nullptr;
	    v.amp = v.targetAmp = 0.0f;
	    v.velocityGain = 1.0f;
        v.sourceMode = kEngineSampleLibrary;
        v.playbackMode = kSamplePlaybackDirect;
        v.elementIndex = kElementHelium;
        v.elementPhase.fill(0.0f);
    for (auto& g : v.grains)
    {
        g.active = false;
        g.reverse = false;
        g.rateScale = 1.0;
        g.pan = 0.0f;
        g.panL = 1.0f;
        g.panR = 1.0f;
    }
    v.uiAmp    .store(0.0f, std::memory_order_relaxed);
    v.uiMidi   .store(-1,   std::memory_order_relaxed);
    v.uiScaleStep.store(-1, std::memory_order_relaxed);
    v.uiSeatRow.store(-1,   std::memory_order_relaxed);
    v.uiSeatCol.store(-1,   std::memory_order_relaxed);
}

void PartialEngine::releaseExtraUnisonVoices (int desiredUnison) noexcept
{
    const int keep = juce::jlimit(1, SeatState::UNISON, desiredUnison);
    for (auto& s : seats)
    {
        for (int i = keep; i < SeatState::UNISON; ++i)
        {
            const int vIdx = s.voiceIdx[(size_t) i].exchange(-1, std::memory_order_relaxed);
            if (vIdx < 0 || vIdx >= MAX_VOICES)
                continue;

            auto& v = voices[(size_t) vIdx];
            if (! v.active)
                continue;

            v.releasing = true;
            v.targetAmp = 0.0f;
        }
    }
}

void PartialEngine::trimVoicesToLimit (int voiceLimit) noexcept
{
    const int limit = juce::jlimit(1, MAX_VOICES, voiceLimit);
    int active = countActiveVoices();

    while (active > limit)
    {
        int bestIdx = -1;
        float bestScore = 1.0e9f;

        for (int i = 0; i < MAX_VOICES; ++i)
        {
            const auto& v = voices[(size_t) i];
            if (! v.active)
                continue;

            float score = v.amp;
            if (i >= limit) score -= 2.0f;
            if (v.releasing) score -= 1.0f;

            if (score < bestScore)
            {
                bestScore = score;
                bestIdx = i;
            }
        }

        if (bestIdx < 0)
            break;

        freeVoice(bestIdx);
        --active;
    }
}

float PartialEngine::computeFilterCoefFromX (float x) const
{
    const float xN     = juce::jlimit(0.0f, 1.0f, x);
    const auto& modeProfile = getModeProfile(signatureMode.load());
    const float b      = juce::jlimit(0.0f, 1.0f,
                                      brightness.load() * 0.72f
                                    + toneMacro.load() * 0.42f
                                    + modeProfile.brightnessOffset);
    constexpr float kbdTrack = 0.20f;
    constexpr float baseCutoff = 5200.0f;
    const float keyboardOctaves = (xN - 0.5f) * kbdTrack * 4.0f;
    const float macroOctaves = (b - 0.55f) * 1.8f;
    const float cutoff = juce::jlimit(18.0f, 22050.0f,
                                      baseCutoff * std::pow(2.0f, keyboardOctaves + macroOctaves));
    const float coef   = 1.0f - std::exp(-kTwoPi * cutoff / (float) sampleRate);
    return juce::jlimit(0.0008f, 0.998f, coef);
}

void PartialEngine::renderVoices (float* L, float* R, int n)
{
    const auto& modeProfile = getModeProfile(signatureMode.load());
    const bool freezeActive = freeze.load() != 0 || modeProfile.reverbFreeze;
    const float energy = juce::jlimit(0.0f, 1.0f, energyMacro.load());
    const float attMs = juce::jmax(1.0f, attackMs .load() * (1.18f - energy * 0.48f) * modeProfile.attackMul);
    const float relMs = juce::jmax(1.0f, releaseMs.load() * (0.82f + energy * 0.28f)
                                               * modeProfile.releaseMul
                                               * (freezeActive ? 8.0f : 1.0f));
    const float attC  = 1.0f - std::exp(-1.0f / (attMs * 0.001f * (float) sampleRate));
    const float relC  = 1.0f - std::exp(-1.0f / (relMs * 0.001f * (float) sampleRate));
    const float lpSmooth = 0.0008f;

    const double globalShift = std::pow(2.0, (double) pitchSemitones.load() / 12.0)
                             * (double) modeProfile.playbackMul;

    float bandAccum[AURORA_BANDS] = { 0.0f };

    int active = 0;
    int registered = juce::jlimit(0, MAX_SEATS,
                                  registeredSeatCount.load(std::memory_order_relaxed));

    const int voiceLimit = getVoiceLimit();
    const int desiredUnison = computeAdaptiveUnisonCount(registered, voiceLimit);
    const int previousUnison = adaptiveUnisonCount.exchange(desiredUnison, std::memory_order_relaxed);
    if (previousUnison != desiredUnison)
        releaseExtraUnisonVoices(desiredUnison);
    trimVoicesToLimit(voiceLimit);

    // Granular parameters (block-rate; signature mode + explicit controls drive them).
    const float move           = juce::jlimit(0.0f, 1.0f,
                                              movement.load() * 0.55f + motionMacro.load() * 0.35f);
    const float density        = juce::jlimit(0.0f, 1.0f,
                                              grainDensity.load() + modeProfile.densityOffset + move * 0.18f);
    const int   numGrains      = 1 + (int) std::round(density * (float) (GRAINS_PER_VOICE - 1));
    const float spreadFraction = juce::jlimit(0.0f, 0.96f,
                                              positionJitter.load() * modeProfile.jitterMul + move * 0.24f);
    const float grainMs        = juce::jlimit(22.0f, 1600.0f,
                                              grainSizeMs.load() * modeProfile.grainSizeMul
                                            * (0.82f + move * 0.36f));
    const int   grainDur       = juce::jmax(64, (int)(grainMs * 0.001f * (float) sampleRate));
    const int   spawnEvery     = juce::jmax(1, grainDur / juce::jmax(1, numGrains));
    const float pitchSpreadSemis = juce::jlimit(0.0f, 24.0f,
                                                pitchSpread.load() + modeProfile.pitchSpreadOffset + move * 0.65f);
    const int   maxPitchDegree = juce::jlimit(0, 14, (int) std::round(pitchSpreadSemis * 0.58f));
    const float reverseChance  = juce::jlimit(0.0f, 0.95f,
                                              (reverseGrains.load() != 0 ? 0.48f : 0.0f)
                                            + modeProfile.reverseChance);
    const float randomStereo   = juce::jlimit(0.0f, 1.0f,
                                              stereoSpread.load() + modeProfile.stereoBoost + move * 0.12f);
    const int   envShape       = grainShape.load();
    // overlap-add level normalization (Hann avg = 0.5, so 2/N preserves unity at N overlap)
    const float grainNorm      = 2.0f / (float) numGrains;

    // Iterate only the active voices instead of sweeping all MAX_VOICES. The
    // active list is sorted ascending, so this visits voices in the exact same
    // order as the old `for (auto& v : voices)` loop -> identical audio output.
    // We snapshot the list (stack, fixed size, no allocation) because the loop
    // body frees finished voices via freeVoice(), which mutates the live list;
    // iterating a snapshot keeps indices valid for the rest of the block. The
    // snapshot is taken after releaseExtraUnisonVoices()/trimVoicesToLimit()
    // above, so it already reflects any voices they freed.
    std::array<int, MAX_VOICES> renderList;
    const int renderCount = activeVoiceCountRT;
    for (int k = 0; k < renderCount; ++k)
        renderList[(size_t) k] = activeVoiceList[(size_t) k];

    for (int k = 0; k < renderCount; ++k)
    {
        auto& v = voices[(size_t) renderList[(size_t) k]];
        if (! v.active) continue;
        ++active;

        if (v.sourceMode == kEngineElementSynth)
        {
            const int element = juce::jlimit(kElementHydrogen, kLastElement, v.elementIndex);
            const auto& spectrum = atomicRawResultForElement(atomicScaleCache->sets, element);
            const int lineCount = (int) spectrum.timbrePartials.size();
            if (lineCount <= 0)
                continue;

            const bool partialSolo = spectralPartialSolo.load(std::memory_order_relaxed) != 0;
            const int selectedPartial = juce::jlimit(0, lineCount - 1,
                                                     spectralPartialCount.load(std::memory_order_relaxed) - 1);
            const int partials = partialSolo ? 1
                                             : juce::jlimit(1, juce::jmin(lineCount, MAX_ELEMENT_PARTIALS),
                                                           spectralPartialCount.load(std::memory_order_relaxed));
            const float stretch = juce::jlimit(-0.35f, 0.35f, spectralStretch.load(std::memory_order_relaxed));
            const float bright = juce::jlimit(0.0f, 1.0f,
                                              brightness.load() * 0.52f
                                            + toneMacro.load() * 0.38f
                                            + modeProfile.brightnessOffset * 0.5f);
            const float baseLevel = partialSolo ? 0.42f : 0.34f / std::sqrt((float) partials);
            const float envC = v.releasing ? relC : attC;
            const float gainScale = v.gainScale;
            const float pitchIncSin = std::sin(v.pitchLfoInc);
            const float pitchIncCos = std::cos(v.pitchLfoInc);
            const float ampIncSin = std::sin(v.ampLfoInc);
            const float ampIncCos = std::cos(v.ampLfoInc);
            std::array<int, MAX_ELEMENT_PARTIALS> partialIndices;
            std::array<double, MAX_ELEMENT_PARTIALS> partialRatios;
            std::array<float, MAX_ELEMENT_PARTIALS> partialAmps;

            for (int pi = 0; pi < partials; ++pi)
            {
                const int partialIndex = partialSolo ? selectedPartial : pi;
                const auto& partial = spectrum.timbrePartials[(size_t) partialIndex];
                const double ratio = std::pow(partial.ratio, 1.0 + (double) stretch);
                const float lineNorm = lineCount > 1 ? (float) partialIndex / (float) (lineCount - 1) : 0.0f;
                const float tilt = 0.62f + bright * (0.58f + lineNorm * 0.82f);
                const float partialAmp = partialSolo ? 1.0f : (float) partial.amp;

                partialIndices[(size_t) pi] = partialIndex;
                partialRatios[(size_t) pi] = ratio;
                partialAmps[(size_t) pi] = partialAmp * tilt * baseLevel;
            }

            for (int i = 0; i < n; ++i)
            {
                const float ampTarget = (freezeActive && v.releasing)
                                      ? juce::jmax(v.targetAmp, v.amp * 0.999995f)
                                      : v.targetAmp;
                v.amp    += (ampTarget      - v.amp)    * envC;
                v.lpCoef += (v.targetLpCoef - v.lpCoef) * lpSmooth;

                const float pitchMod = v.pitchLfoSin * v.pitchLfoDepth;
                const float ampMod = 1.0f + v.ampLfoSin * v.ampLfoDepth;
                const float nextPitchSin = v.pitchLfoSin * pitchIncCos + v.pitchLfoCos * pitchIncSin;
                const float nextPitchCos = v.pitchLfoCos * pitchIncCos - v.pitchLfoSin * pitchIncSin;
                const float nextAmpSin = v.ampLfoSin * ampIncCos + v.ampLfoCos * ampIncSin;
                const float nextAmpCos = v.ampLfoCos * ampIncCos - v.ampLfoSin * ampIncSin;
                v.pitchLfoSin = nextPitchSin;
                v.pitchLfoCos = nextPitchCos;
                v.ampLfoSin = nextAmpSin;
                v.ampLfoCos = nextAmpCos;

                const double pitchRatio = globalShift * (1.0 + pitchMod * 0.0578);
                float mono = 0.0f;
                for (int pi = 0; pi < partials; ++pi)
                {
                    const int partialIndex = partialIndices[(size_t) pi];
                    const double ratio = partialRatios[(size_t) pi];
                    const double partialHz = v.targetFrequencyHz * ratio * pitchRatio;
                    const float amp = partialAmps[(size_t) pi];

                    float& phase = v.elementPhase[(size_t) partialIndex];
                    // Linearly-interpolated sine LUT lookup; phase stays in radians [0, 2*pi).
                    // Mask the integer index to [0, kSineTableSize-1] (power-of-two table) so a
                    // float-rounding edge where t rounds up to kSineTableSize (phase ~= 2*pi)
                    // can never read past the table. idx+1 then stays within the guard slot.
                    const float t = phase * (float) (kSineTableSize / kTwoPi);
                    const int   ti = (int) t;
                    const float frac = t - (float) ti;
                    const int   idx = ti & (kSineTableSize - 1);
                    const float s0 = kSineTable[(size_t) idx];
                    const float s1 = kSineTable[(size_t) idx + 1];
                    mono += (s0 + frac * (s1 - s0)) * amp;
                    phase += (float) (kTwoPi * partialHz / sampleRate);
                    while (phase >= kTwoPi) phase -= kTwoPi;
                    while (phase < 0.0f) phase += kTwoPi;
                }

                v.lpZL += v.lpCoef * (mono - v.lpZL);
                v.lpZR += v.lpCoef * (mono - v.lpZR);

                const float voiceGain = v.amp * gainScale * ampMod;
                L[i] += v.lpZL * voiceGain * v.panL;
                R[i] += v.lpZR * voiceGain * v.panR;
            }

            const float voiceHz = (float) juce::jmax(1.0, v.targetFrequencyHz);
            constexpr float fMin = 20.0f;
            constexpr float fMax = 20000.0f;
            const float lnMin = std::log(fMin);
            const float lnMax = std::log(fMax);
            const float xN = (std::log(juce::jlimit(fMin, fMax, voiceHz)) - lnMin) / (lnMax - lnMin);
            const int bin = juce::jlimit(0, AURORA_BANDS - 1, (int) (xN * (AURORA_BANDS - 1)));
            bandAccum[bin] += v.amp;

            const float pitchNorm = v.pitchLfoSin * v.pitchLfoSin + v.pitchLfoCos * v.pitchLfoCos;
            if (pitchNorm > 0.01f)
            {
                const float inv = 1.0f / std::sqrt(pitchNorm);
                v.pitchLfoSin *= inv;
                v.pitchLfoCos *= inv;
            }

            const float ampNorm = v.ampLfoSin * v.ampLfoSin + v.ampLfoCos * v.ampLfoCos;
            if (ampNorm > 0.01f)
            {
                const float inv = 1.0f / std::sqrt(ampNorm);
                v.ampLfoSin *= inv;
                v.ampLfoCos *= inv;
            }

            v.uiAmp.store(v.amp, std::memory_order_relaxed);

            if (v.releasing && v.amp < 0.0008f)
            {
                freeVoice((int) (&v - voices.data()));
                --active;
            }

            continue;
        }

        if (v.sample == nullptr) continue;

        const auto& buf = v.sample->buffer;
        const int   len = juce::jmin(buf.getNumSamples(), v.sample->trimmedLength);
        if (len <= 0) continue;
        const int    nCh = buf.getNumChannels();
        const float* sL  = buf.getReadPointer(0);
        const float* sR  = nCh > 1 ? buf.getReadPointer(1) : sL;

        if (v.playbackMode == kSamplePlaybackDirect)
        {
            const double baseRate = v.playbackRate * globalShift;
            const float envC = v.releasing ? relC : attC;
            const float gainScale = v.gainScale;
            const float pitchIncSin = std::sin(v.pitchLfoInc);
            const float pitchIncCos = std::cos(v.pitchLfoInc);
            const float ampIncSin = std::sin(v.ampLfoInc);
            const float ampIncCos = std::cos(v.ampLfoInc);
            bool reachedEnd = false;

            for (int i = 0; i < n; ++i)
            {
                if (v.position >= (double) (len - 1))
                {
                    reachedEnd = true;
                    break;
                }

                const float ampTarget = (freezeActive && v.releasing)
                                      ? juce::jmax(v.targetAmp, v.amp * 0.999995f)
                                      : v.targetAmp;
                v.amp    += (ampTarget       - v.amp)    * envC;
                v.lpCoef += (v.targetLpCoef - v.lpCoef) * lpSmooth;

                const float pitchMod = v.pitchLfoSin * v.pitchLfoDepth;
                const float ampMod = 1.0f + v.ampLfoSin * v.ampLfoDepth;
                const float nextPitchSin = v.pitchLfoSin * pitchIncCos + v.pitchLfoCos * pitchIncSin;
                const float nextPitchCos = v.pitchLfoCos * pitchIncCos - v.pitchLfoSin * pitchIncSin;
                const float nextAmpSin = v.ampLfoSin * ampIncCos + v.ampLfoCos * ampIncSin;
                const float nextAmpCos = v.ampLfoCos * ampIncCos - v.ampLfoSin * ampIncSin;
                v.pitchLfoSin = nextPitchSin;
                v.pitchLfoCos = nextPitchCos;
                v.ampLfoSin = nextAmpSin;
                v.ampLfoCos = nextAmpCos;

                const int p0 = juce::jlimit(0, len - 1, (int) v.position);
                const int p1 = juce::jmin(p0 + 1, len - 1);
                const float fr = (float) (v.position - (double) p0);
                const float lIn = sL[(size_t) p0] + fr * (sL[(size_t) p1] - sL[(size_t) p0]);
                const float rIn = sR[(size_t) p0] + fr * (sR[(size_t) p1] - sR[(size_t) p0]);

                v.lpZL += v.lpCoef * (lIn - v.lpZL);
                v.lpZR += v.lpCoef * (rIn - v.lpZR);

                const float voiceGain = v.amp * gainScale * ampMod;
                L[i] += v.lpZL * voiceGain * v.panL;
                R[i] += v.lpZR * voiceGain * v.panR;

                const double rate = baseRate * (1.0 + pitchMod * 0.0578);
                v.position += juce::jmax(0.0, rate);
            }

            const float voiceHz = (float) juce::jmax(1.0, v.targetFrequencyHz);
            constexpr float fMin = 20.0f;
            constexpr float fMax = 20000.0f;
            const float lnMin = std::log(fMin);
            const float lnMax = std::log(fMax);
            const float xN = (std::log(juce::jlimit(fMin, fMax, voiceHz)) - lnMin) / (lnMax - lnMin);
            const int bin = juce::jlimit(0, AURORA_BANDS - 1, (int) (xN * (AURORA_BANDS - 1)));
            bandAccum[bin] += v.amp;

            const float pitchNorm = v.pitchLfoSin * v.pitchLfoSin + v.pitchLfoCos * v.pitchLfoCos;
            if (pitchNorm > 0.01f)
            {
                const float inv = 1.0f / std::sqrt(pitchNorm);
                v.pitchLfoSin *= inv;
                v.pitchLfoCos *= inv;
            }

            const float ampNorm = v.ampLfoSin * v.ampLfoSin + v.ampLfoCos * v.ampLfoCos;
            if (ampNorm > 0.01f)
            {
                const float inv = 1.0f / std::sqrt(ampNorm);
                v.ampLfoSin *= inv;
                v.ampLfoCos *= inv;
            }

            v.uiAmp.store(v.amp, std::memory_order_relaxed);

            if (reachedEnd || (v.releasing && v.amp < 0.0008f))
            {
                freeVoice((int) (&v - voices.data()));
                --active;
            }

            continue;
        }

        const double baseRate = v.playbackRate * globalShift;
        const float  envC = v.releasing ? relC : attC;
        const float  gainScale = v.gainScale;
        const float pitchIncSin = std::sin(v.pitchLfoInc);
        const float pitchIncCos = std::cos(v.pitchLfoInc);
        const float ampIncSin = std::sin(v.ampLfoInc);
        const float ampIncCos = std::cos(v.ampLfoInc);

        for (int i = 0; i < n; ++i)
        {
            const float ampTarget = (freezeActive && v.releasing)
                                  ? juce::jmax(v.targetAmp, v.amp * 0.999995f)
                                  : v.targetAmp;
            v.amp    += (ampTarget       - v.amp)    * envC;
            v.lpCoef += (v.targetLpCoef - v.lpCoef) * lpSmooth;

            const float pitchMod = v.pitchLfoSin * v.pitchLfoDepth;  // semitones
            const float ampMod   = 1.0f + v.ampLfoSin * v.ampLfoDepth;
            const float nextPitchSin = v.pitchLfoSin * pitchIncCos + v.pitchLfoCos * pitchIncSin;
            const float nextPitchCos = v.pitchLfoCos * pitchIncCos - v.pitchLfoSin * pitchIncSin;
            const float nextAmpSin = v.ampLfoSin * ampIncCos + v.ampLfoCos * ampIncSin;
            const float nextAmpCos = v.ampLfoCos * ampIncCos - v.ampLfoSin * ampIncSin;
            v.pitchLfoSin = nextPitchSin;
            v.pitchLfoCos = nextPitchCos;
            v.ampLfoSin = nextAmpSin;
            v.ampLfoCos = nextAmpCos;
            // small-angle approx: 2^(x/12) ~= 1 + 0.0578*x
            const double rate    = baseRate * (1.0 + pitchMod * 0.0578);

            // ---- Maybe spawn a new grain ----
            if (--v.spawnSampleCounter <= 0)
            {
                v.spawnSampleCounter = spawnEvery;
                for (int gi = 0; gi < numGrains; ++gi)
                {
                    auto& g = v.grains[(size_t) gi];
                    if (! g.active)
                    {
                        g.active   = true;
                        g.age      = 0;
                        g.duration = juce::jmax(32, (int) ((float) grainDur * (0.86f + rngVoice.nextFloat() * 0.28f)));
                        g.reverse  = rngVoice.nextFloat() < reverseChance;
                        g.pan      = (rngVoice.nextFloat() * 2.0f - 1.0f) * randomStereo;
                        const float theta = (g.pan + 1.0f) * (kPi * 0.25f);
                        g.panL     = std::cos(theta) * 1.41421356f;
                        g.panR     = std::sin(theta) * 1.41421356f;
                        g.rateScale = 1.0;
                        if (maxPitchDegree > 0)
                        {
                            const int degreeOffset = rngVoice.nextInt(maxPitchDegree * 2 + 1) - maxPitchDegree;
                            const int semis = getScaleDegreeOffsetSemis(v.targetMidi, degreeOffset);
                            g.rateScale = std::pow(2.0, (double) semis / 12.0);
                        }
                        const float jit = (rngVoice.nextFloat() - 0.5f) * spreadFraction;
                        double pos = v.position + (double) jit * (double) len;
                        while (pos < 0.0)            pos += (double) len;
                        while (pos >= (double) len)  pos -= (double) len;
                        g.position = pos;
                        break;
                    }
                }
            }

            // ---- Render all active grains ----
            float lOut = 0.0f, rOut = 0.0f;
            for (int gi = 0; gi < GRAINS_PER_VOICE; ++gi)
            {
                auto& g = v.grains[(size_t) gi];
                if (! g.active) continue;

                const int envIndex = juce::jlimit(0, HANN_LUT_SIZE - 1,
                                                  (g.age * (HANN_LUT_SIZE - 1)) / g.duration);
                const float env = envelopeLuts[(size_t) juce::jlimit(0, 3, envShape)]
                                               [(size_t) envIndex];

                const int    p0 = (int) g.position;
                const int    p1 = g.reverse ? (p0 + len - 1) % len : (p0 + 1) % len;
                const float  fr = (float)(g.position - p0);
                const float  l0 = sL[(size_t) p0] + fr * (sL[(size_t) p1] - sL[(size_t) p0]);
                const float  r0 = sR[(size_t) p0] + fr * (sR[(size_t) p1] - sR[(size_t) p0]);

                lOut += l0 * env * g.panL;
                rOut += r0 * env * g.panR;

                const double grainRate = rate * g.rateScale;
                g.position += g.reverse ? -grainRate : grainRate;
                while (g.position < 0.0)           g.position += (double) len;
                while (g.position >= (double) len) g.position -= (double) len;
                ++g.age;
                if (g.age >= g.duration) g.active = false;
            }

            lOut *= grainNorm;
            rOut *= grainNorm;

            v.lpZL += v.lpCoef * (lOut - v.lpZL);
            v.lpZR += v.lpCoef * (rOut - v.lpZR);

            const float voiceGain = v.amp * gainScale * ampMod;
            L[i] += v.lpZL * voiceGain * v.panL;
            R[i] += v.lpZR * voiceGain * v.panR;

            v.position += freezeActive ? rate * 0.006 : rate;
            while (v.position < 0.0) v.position += (double) len;
            while (v.position >= (double) len) v.position -= (double) len;
        }

        // bin by actual frequency on a 20..20k log scale
	        const float voiceHz = (float) juce::jmax(1.0, v.targetFrequencyHz);
        constexpr float fMin = 20.0f;
        constexpr float fMax = 20000.0f;
        const float lnMin = std::log(fMin);
        const float lnMax = std::log(fMax);
        const float xN    = (std::log(juce::jlimit(fMin, fMax, voiceHz)) - lnMin) / (lnMax - lnMin);
        const int   bin   = juce::jlimit(0, AURORA_BANDS - 1, (int) (xN * (AURORA_BANDS - 1)));
        bandAccum[bin] += v.amp;

        const float pitchNorm = v.pitchLfoSin * v.pitchLfoSin + v.pitchLfoCos * v.pitchLfoCos;
        if (pitchNorm > 0.01f)
        {
            const float inv = 1.0f / std::sqrt(pitchNorm);
            v.pitchLfoSin *= inv;
            v.pitchLfoCos *= inv;
        }

        const float ampNorm = v.ampLfoSin * v.ampLfoSin + v.ampLfoCos * v.ampLfoCos;
        if (ampNorm > 0.01f)
        {
            const float inv = 1.0f / std::sqrt(ampNorm);
            v.ampLfoSin *= inv;
            v.ampLfoCos *= inv;
        }

        v.uiAmp.store(v.amp, std::memory_order_relaxed);

        if (v.releasing && v.amp < 0.0008f)
        {
            freeVoice((int) (&v - voices.data()));
            --active;
        }
    }

    activeVoiceCount.store(active, std::memory_order_relaxed);

    for (int i = 0; i < AURORA_BANDS; ++i)
        auroraBands[(size_t) i].store(bandAccum[i], std::memory_order_relaxed);
}

void PartialEngine::applyReverb (float* L, float* R, int n)
{
    const auto& modeProfile = getModeProfile(signatureMode.load());
    const bool freezeActive = freeze.load() != 0 || modeProfile.reverbFreeze;
    const float amt = juce::jlimit(0.0f, 1.0f,
                                   (reverbAmount.load() * 0.75f
                                  + spaceMacro.load() * 0.42f
                                  + modeProfile.reverbBoost
                                  + (freezeActive ? 0.22f : 0.0f)));
    if (amt < 1.0e-4f) return;

    juce::Reverb::Parameters p;
    p.roomSize   = 0.60f + amt * 0.38f;
    p.damping    = juce::jlimit(0.18f, 0.92f,
                                0.62f - juce::jlimit(0.0f, 1.0f, toneMacro.load()) * 0.30f
                                      - modeProfile.brightnessOffset * 0.35f);
    p.width      = 1.0f;
    p.wetLevel   = amt * 0.9f;
    p.dryLevel   = 1.0f;
    p.freezeMode = freezeActive ? 1.0f : 0.0f;
    reverb.setParameters(p);
    reverb.processStereo(L, R, n);
}

void PartialEngine::applyDelay (float* L, float* R, int n)
{
    const auto& modeProfile = getModeProfile(signatureMode.load());
    const float amt = juce::jlimit(0.0f, 1.0f,
                                   (delayAmount.load() * 0.75f
                                  + spaceMacro.load() * 0.30f
                                  + modeProfile.delayBoost));
    if (amt < 1.0e-4f) return;

    const float feedback = amt * 0.65f;
    const float wet      = amt;
    const int   D        = delaySamples;
    const int   SZ       = MAX_DELAY_SAMPLES;
    int writeIdx = delayWriteIdx;
    int readIdx = writeIdx - D;
    if (readIdx < 0)
        readIdx += SZ;

    for (int i = 0; i < n; ++i)
    {
        const float dlyL  = delayBufL[(size_t) readIdx];
        const float dlyR  = delayBufR[(size_t) readIdx];

        delayBufL[(size_t) writeIdx] = L[i] + dlyR * feedback;
        delayBufR[(size_t) writeIdx] = R[i] + dlyL * feedback;

        L[i] += dlyL * wet;
        R[i] += dlyR * wet;

        if (++readIdx >= SZ)  readIdx = 0;
        if (++writeIdx >= SZ) writeIdx = 0;
    }
    delayWriteIdx = writeIdx;
}

void PartialEngine::applyTapeSaturation (float* L, float* R, int n)
{
    const auto& modeProfile = getModeProfile(signatureMode.load());
    const float drive = juce::jlimit(0.0f, 1.0f, tapeDrive.load() + modeProfile.tapeBoost);
    if (drive < 1.0e-4f)
        return;

    const float inGain  = 1.0f + drive * 5.0f;
    const float outGain = 1.0f / std::tanh(inGain);
    const float trim    = 1.0f - drive * 0.18f;

    for (int i = 0; i < n; ++i)
    {
        L[i] = std::tanh(L[i] * inGain) * outGain * trim;
        R[i] = std::tanh(R[i] * inGain) * outGain * trim;
    }
}

void PartialEngine::applyLimiter (float* L, float* R, int n)
{
    // simple peak limiter, lookahead-free.
    // threshold below 0dBFS so we never reach full scale.
    constexpr float ceiling = 0.92f;
    constexpr float attackPer  = 0.35f;    // per-sample attack toward gainTarget when over
    constexpr float releasePer = 0.00006f; // per-sample release toward 1.0

    // A single NaN/Inf (e.g. from a corrupted feedback path) otherwise sticks in
    // limGain forever, because every comparison against it is false. Sanitize the
    // gain and each sample so a transient non-finite value cannot kill output.
    if (! std::isfinite(limGain))
        limGain = 1.0f;

    for (int i = 0; i < n; ++i)
    {
        float l = std::isfinite(L[i]) ? L[i] : 0.0f;
        float r = std::isfinite(R[i]) ? R[i] : 0.0f;

        const float lN = l * limGain;
        const float rN = r * limGain;
        const float peak = juce::jmax(std::abs(lN), std::abs(rN));

        if (peak > ceiling)
        {
            const float gainTarget = ceiling / juce::jmax(peak / limGain, 1.0e-9f);
            limGain += (gainTarget - limGain) * attackPer;
        }
        else
        {
            limGain += (1.0f - limGain) * releasePer;
        }

        if (! std::isfinite(limGain))
            limGain = 1.0f;

        L[i] = juce::jlimit(-1.0f, 1.0f, l * limGain);
        R[i] = juce::jlimit(-1.0f, 1.0f, r * limGain);
    }
}

void PartialEngine::render (float* outL, float* outR, int numSamples)
{
    std::memset(outL, 0, sizeof(float) * (size_t) numSamples);
    std::memset(outR, 0, sizeof(float) * (size_t) numSamples);

    processPendingCommands();
    drainEvents();
    renderVoices(outL, outR, numSamples);

    const bool canWetDryBlend = dryScratch.getNumChannels() >= 2
                             && dryScratch.getNumSamples() >= numSamples;
    if (canWetDryBlend)
    {
        dryScratch.copyFrom(0, 0, outL, numSamples);
        dryScratch.copyFrom(1, 0, outR, numSamples);
    }

    applyReverb (outL, outR, numSamples);
    applyDelay  (outL, outR, numSamples);

    if (canWetDryBlend)
    {
        const float wet = juce::jlimit(0.0f, 1.0f, wetDry.load());
        const float dry = 1.0f - wet;
        const auto* dryL = dryScratch.getReadPointer(0);
        const auto* dryR = dryScratch.getReadPointer(1);
        for (int i = 0; i < numSamples; ++i)
        {
            outL[i] = dryL[i] * dry + outL[i] * wet;
            outR[i] = dryR[i] * dry + outR[i] * wet;
        }
    }

    // master gain (pre-limiter)
    const float master = masterGain.load();
    for (int i = 0; i < numSamples; ++i)
    {
        outL[i] *= master;
        outR[i] *= master;
    }

    applyTapeSaturation(outL, outR, numSamples);
    applyLimiter(outL, outR, numSamples);
    audioCallbackSampleClock += (uint64_t) juce::jmax(0, numSamples);
}

void PartialEngine::processControlEvents (int numSamples)
{
    processPendingCommands();
    drainEvents();
    audioCallbackSampleClock += (uint64_t) juce::jmax(0, numSamples);
}
