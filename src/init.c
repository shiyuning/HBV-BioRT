#include "biort.h"

// Initialize RT structures
void InitChem(const char dir[], int nsub, const calib_struct *calib, const ctrl_struct *ctrl, chemtbl_struct chemtbl[],
    kintbl_struct kintbl[], rttbl_struct *rttbl, subcatch_struct subcatch[])
{
    char            fn[MAXSTRING];
    int             kspc;
    int             ksub;
    FILE           *fp;

    // LOOK UP DATABASE TO FIND REQUIRED PARAMETERS AND DEPENDENCIES FOR CHEMICAL SPECIES IN CDBS.TXT
    sprintf(fn, "input/%s/cdbs.txt", dir);
    fp = fopen(fn, "r");

    Lookup(fp, calib, chemtbl, kintbl, rttbl);
    fclose(fp);

    for (ksub = 0; ksub < nsub; ksub++)
    {
        for (kspc = 0; kspc < rttbl->num_stc; kspc++)
        {
            // Apply calibration
            subcatch[ksub].chms[UZ].ssa[kspc] *= (chemtbl[kspc].itype == MINERAL) ? calib->ssa : 1.0;
            subcatch[ksub].chms[LZ].ssa[kspc] *= (chemtbl[kspc].itype == MINERAL) ? calib->ssa : 1.0;

            // Snow and soil moisture zone should have the same concentrations as the upper zone at the beginning
            subcatch[ksub].chms[SNSM].tot_conc[kspc] = subcatch[ksub].chms[UZ].tot_conc[kspc];
            subcatch[ksub].chms[SNSM].prim_conc[kspc] = subcatch[ksub].chms[SNSM].tot_conc[kspc];
            subcatch[ksub].chms[SNSM].ssa[kspc] = subcatch[ksub].chms[UZ].ssa[kspc];
            subcatch[ksub].chms[SNSM].tot_mol[kspc] =
                subcatch[ksub].chms[SNSM].tot_conc[kspc] * subcatch[ksub].ws[0][SNSM];
        }

        // Initialize upper and lower zone concentrations taking into account speciation
        InitChemState(subcatch[ksub].porosity_uz, subcatch[ksub].ws[0][UZ], chemtbl, rttbl, ctrl,
            &subcatch[ksub].chms[UZ]);
        InitChemState(subcatch[ksub].porosity_lz, subcatch[ksub].ws[0][LZ], chemtbl, rttbl, ctrl,
            &subcatch[ksub].chms[LZ]);
    }
}

void InitChemState(double smcmax, double vol, const chemtbl_struct chemtbl[], const rttbl_struct *rttbl,
    const ctrl_struct *ctrl, chmstate_struct *chms)
{
    int             kspc;

    for (kspc = 0; kspc < rttbl->num_stc; kspc++)
    {
        if (strcmp(chemtbl[kspc].name, "'H+'") == 0)
        {
            chms->prim_actv[kspc] = chms->tot_conc[kspc];
            chms->prim_conc[kspc] = chms->tot_conc[kspc];
        }
        else if (chemtbl[kspc].itype == MINERAL)
        {
            // Update the concentration of mineral using molar volume
            chms->tot_conc[kspc] *= (ctrl->rel_min == 0) ?
                1000.0 / chemtbl[kspc].molar_vol / smcmax :     // Absolute mineral volume fraction
                (1.0 - smcmax) * 1000.0 / chemtbl[kspc].molar_vol / smcmax;     // Relative mineral volume fraction
            chms->prim_actv[kspc] = 1.0;
            chms->prim_conc[kspc] = chms->tot_conc[kspc];
        }
        else if ((chemtbl[kspc].itype == CATION_ECHG) || (chemtbl[kspc].itype == ADSORPTION))
        {
            chms->prim_actv[kspc] = chms->tot_conc[kspc] * 0.5;
            chms->tot_conc[kspc] *= (1.0 - smcmax) * 2650.0;    // Change unit of CEC (eq g-1) into C(ion site)
                                                                // (eq L-1 porous space), assuming density of solid is
                                                                // always 2650 g L-1
            chms->prim_conc[kspc] = chms->tot_conc[kspc];
        }
        else
        {
            chms->prim_actv[kspc] = chms->tot_conc[kspc] * 0.5;
            chms->prim_conc[kspc] = chms->tot_conc[kspc] * 0.5;
        }
    }

    for (kspc = 0; kspc < rttbl->num_ssc; kspc++)
    {
        chms->sec_conc[kspc] = ZERO_CONC;
    }

    // Speciation
    SolveSpeciation(chemtbl, ctrl, rttbl, 1, chms);

    // Total moles should be calculated after speciation
    for (kspc = 0; kspc < rttbl->num_stc; kspc++)
    {
        chms->tot_mol[kspc] = chms->tot_conc[kspc] * vol;
    }
}
