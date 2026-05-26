#include "PartialEngine.h"
#include "AtomicScaleBuilder.h"
#include <cmath>
#include <cstring>
#include <limits>

namespace
{
    constexpr float kPi    = 3.14159265358979323846f;
    constexpr float kTwoPi = 6.28318530717958647692f;

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
	    constexpr int kTotalScaleModes = kNeonMode + 1;
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
        constexpr int kLastElement = kElementNeon;

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

	    const ScaleDef& getScaleDef (int mode)
	    {
	        return kScales[(size_t) juce::jlimit(0, (int) kScales.size() - 1, mode)];
	    }

	    bool isSpectralMode (int mode) noexcept
	    {
	        return mode >= kHydrogenMode && mode <= kNeonMode;
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
            switch (juce::jlimit(kElementHydrogen, kLastElement, element))
            {
	                case kElementHydrogen:   return hydrogenLines();
	                case kElementLithium:    return lithiumLines();
	                case kElementBeryllium:  return berylliumLines();
                    case kElementBoron:      return boronLines();
                    case kElementCarbon:     return carbonLines();
                    case kElementOxygen:     return oxygenLines();
                    case kElementFluorine:   return fluorineLines();
                    case kElementNeon:       return neonLines();
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

        const AtomicScaleBuilder::Result& atomicResultFor (int element, AtomicScaleBuilder::ScaleMode mode)
        {
            struct ResultSet
            {
                AtomicScaleBuilder::Result melodic, performable, microtonal, scientific, raw;
            };

            static const std::array<ResultSet, kLastElement + 1> cache {{
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
            }};

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

        const AtomicScaleBuilder::Result& atomicScaleResultForMode (int spectralMode, int atomicModeIndex)
        {
            return atomicResultFor(elementForSpectralMode(spectralMode),
                                   scaleModeForAtomicIndex(atomicModeIndex));
        }

        const AtomicScaleBuilder::Result& atomicRawResultForElement (int element)
        {
            return atomicResultFor(element, AtomicScaleBuilder::ScaleMode::Raw);
        }

        int spectralLineCount (int mode, int atomicModeIndex) noexcept
        {
            if (! isSpectralMode(mode))
                return 0;

            return (int) atomicScaleResultForMode(mode, atomicModeIndex).scaleDegrees.size();
        }

        int elementLineCount (int element) noexcept
        {
            return (int) atomicRawResultForElement(element).timbrePartials.size();
        }

        double elementReferenceWavelength (int element) noexcept
        {
            return atomicRawResultForElement(element).lambdaRefNm;
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

PartialEngine::PartialEngine()
{
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
    for (int element = kElementHydrogen; element <= kLastElement; ++element)
        for (int mode = 0; mode < 5; ++mode)
            (void) atomicResultFor(element, scaleModeForAtomicIndex(mode));

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
            v.elementPartials = 0;
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
        ? spectralLineCount(mode, atomicMode)
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
    return isSpectralMode(mode) ? spectralLineCount(mode, atomicScaleMode.load(std::memory_order_relaxed))
                                : getScaleDef(mode).count;
}

PartialEngine::PitchTarget PartialEngine::getScalePitch (int idx) const noexcept
{
    const int mode = juce::jlimit(0, kTotalScaleModes - 1,
                                  scaleMode.load(std::memory_order_relaxed));
    const int octaves = juce::jmax(1, scaleOctaves.load(std::memory_order_relaxed));

    if (isSpectralMode(mode))
    {
        const int atomicMode = atomicScaleMode.load(std::memory_order_relaxed);
        const auto& scale = atomicScaleResultForMode(mode, atomicMode);
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
    buildScaleTable();
    if (n > 0)
        retriggerActiveSeats();
    return n;
}

void PartialEngine::buildScaleTable()
{
    int count = 0;
    const int mode = juce::jlimit(0, kTotalScaleModes - 1,
                                  scaleMode.load(std::memory_order_relaxed));
    if (library.numSamples() > 0 && ! isSpectralMode(mode))
    {
        int minM = 127, maxM = 0;
        for (int i = 0; i < library.numSamples(); ++i)
        {
            if (auto* s = library.getSample(i))
            {
                minM = juce::jmin(minM, s->rootMidi);
                maxM = juce::jmax(maxM, s->rootMidi);
            }
        }
        const auto& scale = getScaleDef(mode);
        // include every active-scale note in [minM, maxM]
        for (int m = minM; m <= maxM && count < (int) scaleTable.size(); ++m)
        {
            const int pc = ((m % 12) + 12) % 12;
            for (int i = 0; i < scale.count; ++i)
                if (pc == scale.degrees[(size_t) i]) { scaleTable[(size_t) count++] = m; break; }
        }
    }
    scaleTableCount.store(count, std::memory_order_release);
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

    const auto& scale = atomicScaleResultForMode(mode, atomicScaleMode.load(std::memory_order_relaxed));
    const int lineCount = (int) scale.scaleDegrees.size();
    if (lineCount <= 0)
        return 0.0;

    return scale.scaleDegrees[(size_t) (idx % lineCount)].representativeWavelengthNm;
}

float PartialEngine::getScaleLineAmplitude (int idx) const noexcept
{
    const int mode = juce::jlimit(0, kTotalScaleModes - 1,
                                  scaleMode.load(std::memory_order_relaxed));
    if (! isSpectralMode(mode))
        return 0.0f;

    const auto& scale = atomicScaleResultForMode(mode, atomicScaleMode.load(std::memory_order_relaxed));
    const int lineCount = (int) scale.scaleDegrees.size();
    if (lineCount <= 0)
        return 0.0f;

    return (float) scale.scaleDegrees[(size_t) (idx % lineCount)].velocity;
}

int PartialEngine::getScaleStepMidi (int step) const noexcept
{
    const int mode = juce::jlimit(0, kTotalScaleModes - 1,
                                  scaleMode.load(std::memory_order_relaxed));
    if (isSpectralMode(mode))
    {
        const int lineCount = spectralLineCount(mode, atomicScaleMode.load(std::memory_order_relaxed));
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
        const auto& scale = atomicScaleResultForMode(mode, atomicMode);
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
        const auto& scale = atomicScaleResultForMode(mode, atomicMode);

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
    return elementReferenceWavelength(element);
}

int PartialEngine::getSpectralElementLineCount() const noexcept
{
    const int element = juce::jlimit(kElementHydrogen, kLastElement,
                                     spectralElement.load(std::memory_order_relaxed));
    return elementLineCount(element);
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
            v.elementPartials = 0;
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
}

void PartialEngine::clearAllSeats()
{
    eventFifo.reset();
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
    eventFifo.reset();
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

void PartialEngine::releaseAllKeyboardNotes()
{
    for (int slot = 0; slot < MAX_KEYBOARD_SLOTS; ++slot)
        setKeyboardStep(slot, 0, 0.0f, false);
}

void PartialEngine::enqueueEvent (const VoiceEvent& e)
{
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

    auto& state = keyboardSlots[(size_t) slot];
    if (state.currentMidi >= 0)
        enqueueSeatMidiNoteOff(-1, slot, keyboardSourceId(slot));

    for (auto& vi : state.voiceIdx)
    {
        if (vi >= 0)
            freeVoice(vi);
        vi = -1;
    }

    const int total = getScaleTableSize();
    if (total <= 0)
        return;

    const int safeStep = juce::jlimit(0, total - 1, scaleStep);
    const auto target = getScalePitch(safeStep);
    const float x = total > 1 ? (float) safeStep / (float) (total - 1) : 0.5f;
    const float y = juce::jlimit(0.0f, 1.0f, velocity);
    const int voiceLimit = getVoiceLimit();
    const int activeSources = juce::jmax(1, registeredSeatCount.load(std::memory_order_relaxed) + 1);
    const int unisonCount = computeAdaptiveUnisonCount(activeSources, voiceLimit);
    adaptiveUnisonCount.store(unisonCount, std::memory_order_relaxed);

    const int panCol = juce::jlimit(0, MAX_COLS - 1,
                                    (int) std::round(x * (float) (MAX_COLS - 1)));
    const int sideSign = (slot & 1) == 0 ? 1 : -1;
    const float centerGain = unisonCount == 1 ? 0.86f : 0.55f;
    const float sideGain   = unisonCount == 2 ? 0.50f : 0.42f;

    state.voiceIdx[0] = allocateVoice(-1, panCol, target.midi, target.frequencyHz, target.velocityGain,
                                       0.0f, centerGain, 0.0f, x, y);
    state.voiceIdx[1] = unisonCount >= 2
        ? allocateVoice(-1, panCol, target.midi, target.frequencyHz, target.velocityGain,
                        (float) sideSign * 7.0f, sideGain, (float) sideSign * 0.18f, x, y)
        : -1;
    state.voiceIdx[2] = unisonCount >= 3
        ? allocateVoice(-1, panCol, target.midi, target.frequencyHz, target.velocityGain,
                        (float) -sideSign * 7.0f, 0.42f, (float) -sideSign * 0.18f, x, y)
        : -1;

    state.active = true;
    state.step = safeStep;
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
                                 0.0f, centerGain, 0.0f, x, y);
    const int v1 = unisonCount >= 2
                 ? allocateVoice(row, col, newMidi, target.frequencyHz, target.velocityGain,
                                 (float) sideSign * 7.0f, sideGain,
                                 (float) sideSign * 0.18f, x, y)
                 : -1;
    const int v2 = unisonCount >= 3
                 ? allocateVoice(row, col, newMidi, target.frequencyHz, target.velocityGain,
                                 (float) -sideSign * 7.0f, 0.42f,
                                 (float) -sideSign * 0.18f, x, y)
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

int PartialEngine::allocateVoice (int row, int col, int midi,
                                  double frequencyHz, float velocityGain,
                                  float detuneCents, float gainScale, float panOffset,
                                  float x, float y)
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
    v.elementPartials = juce::jlimit(1, MAX_ELEMENT_PARTIALS,
                                     elementLineCount(v.elementIndex));
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
	    v.seatRow = v.seatCol = -1;
	    v.targetMidi = 60;
	    v.targetFrequencyHz = midiToHz(60);
	    v.sample = nullptr;
	    v.amp = v.targetAmp = 0.0f;
	    v.velocityGain = 1.0f;
        v.sourceMode = kEngineSampleLibrary;
        v.playbackMode = kSamplePlaybackDirect;
        v.elementIndex = kElementHelium;
        v.elementPartials = 0;
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

    for (auto& v : voices)
    {
        if (! v.active) continue;
        ++active;

        if (v.sourceMode == kEngineElementSynth)
        {
            const int element = juce::jlimit(kElementHydrogen, kLastElement, v.elementIndex);
            const auto& spectrum = atomicRawResultForElement(element);
            const int lineCount = (int) spectrum.timbrePartials.size();
            if (lineCount <= 0)
                continue;

            const bool partialSolo = spectralPartialSolo.load(std::memory_order_relaxed) != 0;
            const int selectedPartial = juce::jlimit(0, lineCount - 1,
                                                     spectralPartialCount.load(std::memory_order_relaxed) - 1);
            const int partials = partialSolo ? 1
                                             : juce::jlimit(1, juce::jmin(lineCount, MAX_ELEMENT_PARTIALS),
                                                           v.elementPartials);
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
                    const int partialIndex = partialSolo ? selectedPartial : pi;
                    const auto& partial = spectrum.timbrePartials[(size_t) partialIndex];
                    const double rawRatio = partial.ratio;
                    const double ratio = std::pow(rawRatio, 1.0 + (double) stretch);
                    const double partialHz = v.targetFrequencyHz * ratio * pitchRatio;
                    const float lineNorm = lineCount > 1 ? (float) partialIndex / (float) (lineCount - 1) : 0.0f;
                    const float tilt = 0.62f + bright * (0.58f + lineNorm * 0.82f);
                    const float partialAmp = partialSolo ? 1.0f : (float) partial.amp;
                    const float amp = partialAmp * tilt * baseLevel;

                    float& phase = v.elementPhase[(size_t) partialIndex];
                    mono += std::sin(phase) * amp;
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

    for (int i = 0; i < n; ++i)
    {
        const float lN = L[i] * limGain;
        const float rN = R[i] * limGain;
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

        L[i] = juce::jlimit(-1.0f, 1.0f, L[i] * limGain);
        R[i] = juce::jlimit(-1.0f, 1.0f, R[i] * limGain);
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
