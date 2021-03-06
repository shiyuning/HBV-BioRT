#include "biort.h"

int SolveSpeciation(const chemtbl_struct chemtbl[], const ctrl_struct *ctrl, const rttbl_struct *rttbl, int speciation_flg,
    chmstate_struct *chms)
{
    int             i, j, k;
    int             jcb_dim;
    double          residue[MAXSPS];
    double          tmpconc[MAXSPS];
    double          tot_conc[MAXSPS];
    double          gamma[MAXSPS];
    double          maxerror;
    realtype      **jcb;
    const double    TMPPRB = 1E-2;

    // If speciation flg = 1, pH is defined. Total concentration is calculated from the activity of H+. Dependency is
    // the same but the total concentration for H+ does not need to be solved.
    // If speciation flg = 0, all defined value is total concentration
    for (k = 0; k < MAXSPS; k++)
    {
        residue[k] = 0.0;
        tmpconc[k] = 0.0;
        tot_conc[k] = 0.0;
        gamma[k] = 0.0;
    }

    for (i = 0; i < rttbl->num_stc; i++)
    {
        // Using log10 conc as the primary unknowns. Works better because negative numbers are not a problem.
        tmpconc[i] = log10(chms->prim_conc[i]);
    }

    for (i = 0; i < rttbl->num_ssc; i++)
    {
        tmpconc[i + rttbl->num_stc] = 0.0;
        for (j = 0; j < rttbl->num_sdc; j++)
        {
            tmpconc[i + rttbl->num_stc] += tmpconc[j] * rttbl->dep_mtx[i][j];
        }
        tmpconc[i + rttbl->num_stc] -= rttbl->keq[i];
    }

    jcb_dim = (speciation_flg == 1) ? rttbl->num_stc - 1: rttbl->num_stc;

    jcb = newDenseMat(jcb_dim, jcb_dim);

    do
    {
        sunindextype    p[MAXSPS];
        realtype        x[MAXSPS];
        int             row, col;

        if (ctrl->actv_mode == 1)
        {
            double          imat = 0.0;
            double          iroot;

            // Calculate the ionic strength in this block
            for (i = 0; i < rttbl->num_stc + rttbl->num_ssc; i++)
            {
                imat += 0.5 * pow(10, tmpconc[i]) * chemtbl[i].charge * chemtbl[i].charge;
            }
            iroot = sqrt(imat);

            for (i = 0; i < rttbl->num_stc + rttbl->num_ssc; i++)
            {
                // Aqueous species in the unit of mol/L, however the solids are in the unit of mol/L porous media.
                // Activity of solid is 1, log10 of activity is 0. By assigning gamma[minerals] to negative of the
                // tmpconc[minerals], we ensured the log10 of activity of solids are 0. gamma stores log10gamma[i]
                gamma[i] = (chemtbl[i].itype == MINERAL) ? -tmpconc[i] :
                    (-rttbl->adh * iroot * chemtbl[i].charge * chemtbl[i].charge) /
                    (1.0 + rttbl->bdh * chemtbl[i].size_fac * iroot) + rttbl->bdt * imat;

                if (speciation_flg == 1 && strcmp(chemtbl[i].name, "'H+'") == 0)
                {
                    tmpconc[i] = log10(chms->prim_actv[i]) - gamma[i];
                }
            }
        }

        for (i = 0; i < rttbl->num_ssc; i++)
        {
            tmpconc[i + rttbl->num_stc] = 0.0;
            for (j = 0; j < rttbl->num_sdc; j++)
            {
                tmpconc[i + rttbl->num_stc] += (tmpconc[j] + gamma[j]) * rttbl->dep_mtx[i][j];
            }
            tmpconc[i + rttbl->num_stc] -= rttbl->keq[i] + gamma[i + rttbl->num_stc];
        }

        for (i = 0; i < rttbl->num_stc; i++)
        {
            // Update the total concentration of H+ for later stage RT at initialization
            tot_conc[i] = 0.0;
            for (j = 0; j < rttbl->num_stc + rttbl->num_ssc; j++)
            {
                tot_conc[i] += rttbl->conc_contrib[i][j] * pow(10, tmpconc[j]);
            }

            if (speciation_flg == 1 && strcmp(chemtbl[i].name, "'H+'") == 0)
            {
                chms->tot_conc[i] = tot_conc[i];
            }

            residue[i] = tot_conc[i] - chms->tot_conc[i];
        }


        col = 0;
        for (k = 0; k < rttbl->num_stc; k++)
        {
            if (speciation_flg == 1 && strcmp(chemtbl[k].name, "'H+'") == 0)
            {
                continue;
            }

            tmpconc[k] += TMPPRB;
            for (i = 0; i < rttbl->num_ssc; i++)
            {
                tmpconc[i + rttbl->num_stc] = 0.0;
                for (j = 0; j < rttbl->num_sdc; j++)
                {
                    tmpconc[i + rttbl->num_stc] += (tmpconc[j] + gamma[j]) * rttbl->dep_mtx[i][j];
                }
                tmpconc[i + rttbl->num_stc] -= rttbl->keq[i] + gamma[i + rttbl->num_stc];
            }

            row = 0;
            for (i = 0; i < rttbl->num_stc; i++)
            {
                double          tmpval;

                if (speciation_flg == 1 && strcmp(chemtbl[i].name, "'H+'") == 0)
                {
                    continue;
                }

                tmpval = 0.0;
                for (j = 0; j < rttbl->num_stc + rttbl->num_ssc; j++)
                {
                    tmpval += rttbl->conc_contrib[i][j] * pow(10, tmpconc[j]);
                }

                jcb[col][row] = (tmpval - chms->tot_conc[i] - residue[i]) / TMPPRB;

                row++;
            }

            tmpconc[k] -= TMPPRB;

            col++;
        }

        row = 0;
        for (i = 0; i < rttbl->num_stc; i++)
        {
            if (speciation_flg == 1 && strcmp(chemtbl[i].name, "'H+'") == 0)
            {
                continue;
            }

            x[row++] = -residue[i];
        }

        if (denseGETRF(jcb, jcb_dim, jcb_dim, p) != 0)
        {
            biort_printf(VL_ERROR, "Speciation error.\n");
            exit(EXIT_FAILURE);
        }

        denseGETRS(jcb, jcb_dim, p, x);

        maxerror = 0.0;
        row = 0;
        for (i = 0; i < rttbl->num_stc; i++)
        {
            if (speciation_flg == 1 && strcmp(chemtbl[i].name, "'H+'") == 0)
            {
                continue;
            }

            tmpconc[i] += x[row++];
            maxerror = MAX(fabs(residue[i] / tot_conc[i]), maxerror);
        }
    } while (maxerror > TOLERANCE);

    for (i = 0; i < rttbl->num_ssc; i++)
    {
        tmpconc[i + rttbl->num_stc] = 0.0;
        for (j = 0; j < rttbl->num_sdc; j++)
        {
            tmpconc[i + rttbl->num_stc] += (tmpconc[j] + gamma[j]) * rttbl->dep_mtx[i][j];
        }
        tmpconc[i + rttbl->num_stc] -= rttbl->keq[i] + gamma[i + rttbl->num_stc];
    }

    for (i = 0; i < rttbl->num_stc; i++)
    {
        tot_conc[i] = 0.0;
        for (j = 0; j < rttbl->num_stc + rttbl->num_ssc; j++)
        {
            tot_conc[i] += rttbl->conc_contrib[i][j] * pow(10, tmpconc[j]);
        }
        residue[i] = tot_conc[i] - chms->tot_conc[i];
    }

    for (i = 0; i < rttbl->num_stc + rttbl->num_ssc; i++)
    {
        if (i < rttbl->num_stc)
        {
            if (chemtbl[i].itype == MINERAL)
            {
                chms->prim_conc[i] = pow(10, tmpconc[i]);
                chms->prim_actv[i] = 1.0;
            }
            else
            {
                chms->prim_conc[i] = pow(10, tmpconc[i]);
                chms->prim_actv[i] = pow(10, (tmpconc[i] + gamma[i]));
            }
        }
        else
        {
            chms->sec_conc[i - rttbl->num_stc] = pow(10, tmpconc[i]);
#if TEMP_DISABLED
            chms->s_actv[i - rttbl->num_stc] = pow(10, (tmpconc[i] + gamma[i]));
#endif
        }
    }

    destroyMat(jcb);

    return 0;
}

void Speciation(int nsub, const chemtbl_struct chemtbl[], const ctrl_struct *ctrl, const rttbl_struct *rttbl,
    subcatch_struct subcatch[])
{
    int             ksub;

    for (ksub = 0; ksub < nsub; ksub++)
    {
        SolveSpeciation(chemtbl, ctrl, rttbl, 0, &subcatch[ksub].chms[UZ]);
        SolveSpeciation(chemtbl, ctrl, rttbl, 0, &subcatch[ksub].chms[LZ]);
    }
}

void StreamSpeciation(int step, int nsub, const chemtbl_struct chemtbl[], const ctrl_struct *ctrl,
    const rttbl_struct *rttbl, subcatch_struct subcatch[])
{
    int             ksub;
    int             kspc;
    static int      init_flag = 1;

    // In the first model step, initialize primary concentrations, activities in river. This cannot be done during
    // initialization step because stream concentrations are unknown
    if (init_flag == 1)
    {
        for (ksub = 0; ksub < nsub; ksub++)
        {
            for (kspc = 0; kspc < rttbl->num_stc; kspc++)
            {
                if (chemtbl[kspc].itype == AQUEOUS)
                {
                    subcatch[ksub].chms[STREAM].prim_actv[kspc] = subcatch[ksub].chms[STREAM].tot_conc[kspc];
                    subcatch[ksub].chms[STREAM].prim_conc[kspc] = subcatch[ksub].chms[STREAM].tot_conc[kspc];
                }
                else
                {
                    subcatch[ksub].chms[STREAM].tot_conc[kspc] = ZERO_CONC;
                    subcatch[ksub].chms[STREAM].prim_conc[kspc] = ZERO_CONC;
                    subcatch[ksub].chms[STREAM].prim_actv[kspc] = ZERO_CONC;
                    subcatch[ksub].chms[STREAM].tot_mol[kspc] = 0.0;
                }
            }

            for (kspc = 0; kspc < rttbl->num_ssc; kspc++)
            {
                subcatch[ksub].chms[STREAM].sec_conc[kspc] = ZERO_CONC;
            }
        }
    }

    init_flag = 0;

    for (ksub = 0; ksub < nsub; ksub++)
    {
        if (subcatch[ksub].q[step][Q0] + subcatch[ksub].q[step][Q1] + subcatch[ksub].q[step][Q2] <= 0.0)
        {
            for (kspc = 0; kspc < rttbl->num_stc; kspc++)
            {
                if (chemtbl[kspc].itype == AQUEOUS)
                {
                    subcatch[ksub].chms[STREAM].prim_actv[kspc] = subcatch[ksub].chms[STREAM].tot_conc[kspc];
                    subcatch[ksub].chms[STREAM].prim_conc[kspc] = subcatch[ksub].chms[STREAM].tot_conc[kspc];
                }
                else
                {
                    subcatch[ksub].chms[STREAM].tot_conc[kspc] = ZERO_CONC;
                    subcatch[ksub].chms[STREAM].prim_conc[kspc] = ZERO_CONC;
                    subcatch[ksub].chms[STREAM].prim_actv[kspc] = ZERO_CONC;
                    subcatch[ksub].chms[STREAM].tot_mol[kspc] = 0.0;
                }
            }

            for (kspc = 0; kspc < rttbl->num_ssc; kspc++)
            {
                subcatch[ksub].chms[STREAM].sec_conc[kspc] = ZERO_CONC;
            }
        }
        else
        {
            SolveSpeciation(chemtbl, ctrl, rttbl, 0, &subcatch[ksub].chms[STREAM]);
        }
    }
}
