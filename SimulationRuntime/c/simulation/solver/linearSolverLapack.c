/*
 * This file is part of OpenModelica.
 *
 * Copyright (c) 1998-CurrentYear, Linköping University,
 * Department of Computer and Information Science,
 * SE-58183 Linköping, Sweden.
 *
 * All rights reserved.
 *
 * THIS PROGRAM IS PROVIDED UNDER THE TERMS OF GPL VERSION 3
 * AND THIS OSMC PUBLIC LICENSE (OSMC-PL).
 * ANY USE, REPRODUCTION OR DISTRIBUTION OF THIS PROGRAM CONSTITUTES RECIPIENT'S
 * ACCEPTANCE OF THE OSMC PUBLIC LICENSE.
 *
 * The OpenModelica software and the Open Source Modelica
 * Consortium (OSMC) Public License (OSMC-PL) are obtained
 * from Linköping University, either from the above address,
 * from the URLs: http://www.ida.liu.se/projects/OpenModelica or
 * http://www.openmodelica.org, and in the OpenModelica distribution.
 * GNU version 3 is obtained from: http://www.gnu.org/copyleft/gpl.html.
 *
 * This program is distributed WITHOUT ANY WARRANTY; without
 * even the implied warranty of  MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE, EXCEPT AS EXPRESSLY SET FORTH
 * IN THE BY RECIPIENT SELECTED SUBSIDIARY LICENSE CONDITIONS
 * OF OSMC-PL.
 *
 * See the full OSMC Public License conditions for more details.
 *
 */

/*! \file nonlinear_solver.c
 */

#include <math.h>
#include <stdlib.h>
#include <string.h> /* memcpy */

#include "simulation_data.h"
#include "omc_error.h"
#include "varinfo.h"
#include "model_help.h"

#include "linearSystem.h"
#include "linearSolverLapack.h"
#include "blaswrap.h"
#include "f2c.h"
extern int dgesv_(integer *n, integer *nrhs, doublereal *a, integer *lda,
                  integer *ipiv, doublereal *b, integer *ldb, integer *info);

typedef struct DATA_LAPACK
{
  integer *ipiv;  /* vector pivot values */
  integer nrhs;   /* number of righthand sides*/
  integer info;   /* output */
} DATA_LAPACK;

/*! \fn allocate memory for linear system solver lapack
 *
 */
int allocateLapackData(int size, void** voiddata)
{
  DATA_LAPACK* data = (DATA_LAPACK*) malloc(sizeof(DATA_LAPACK));

  data->ipiv = (integer*) malloc(size*sizeof(modelica_integer));
  assertStreamPrint(0 != data->ipiv, "Could not allocate data for linear solver lapack.");
  data->nrhs = 1;
  data->info = 0;

  *voiddata = (void*)data;
  return 0;
}

/*! \fn free memory for nonlinear solver hybrd
 *
 */
int freeLapackData(void **voiddata)
{
  DATA_LAPACK* data = (DATA_LAPACK*) *voiddata;

  free(data->ipiv);

  return 0;
}

/*! \fn solve linear system with lapack method
 *
 *  \param [in]  [data]
 *                [sysNumber] index of the corresponing non-linear system
 *
 *  \author wbraun
 */
int solveLapack(DATA *data, int sysNumber)
{
  int i, j;
  LINEAR_SYSTEM_DATA* systemData = &(data->simulationInfo.linearSystemData[sysNumber]);
  DATA_LAPACK* solverData = (DATA_LAPACK*)systemData->solverData;
  int n = systemData->size;

  /* We are given the number of the linear system.
   * We want to look it up among all equations. */
  /* int eqSystemNumber = systemData->equationIndex; */
  int success = 1;

  /* reset matrix A */
  memset(systemData->A, 0, (systemData->size)*(systemData->size)*sizeof(double));

  /* update matrix A */
  systemData->setA(data, systemData);
  /* update vector b (rhs) */
  systemData->setb(data, systemData);

  /* Log A*x=b */
  if(ACTIVE_STREAM(LOG_LS_V))
  {
    char buffer[16384];
  
  /* A matrix */
    infoStreamPrint(LOG_LS_V, 1, "A matrix [%dx%d]", n, n);
    for(i=0; i<n; i++)
    {
      buffer[0] = 0;
      for(j=0; j<n; j++)
        sprintf(buffer, "%s%20.12g ", buffer, systemData->A[i + j*n]);
      infoStreamPrint(LOG_LS_V, 0, "%s", buffer);
    }
  
  /* b vector */
    infoStreamPrint(LOG_LS_V, 1, "b vector [%d]", n);
    for(i=0; i<n; i++)
    {
      buffer[0] = 0;
      sprintf(buffer, "%s%20.12g ", buffer, systemData->b[i]);
      infoStreamPrint(LOG_LS_V, 0, "%s", buffer);
    }
  
    messageClose(LOG_LS_V);
  }
 
  /* Solve system */ 
  dgesv_((integer*) &systemData->size,
         (integer*) &solverData->nrhs,
         systemData->A,
         (integer*) &systemData->size,
         solverData->ipiv,
         systemData->b,
         (integer*) &systemData->size,
         &solverData->info);

  if(solverData->info < 0)
  {
    warningStreamPrint(LOG_STDOUT, 0, "Error solving linear system of equations (no. %d) at time %f. Argument %d illegal.", (int)systemData->equationIndex, data->localData[0]->timeValue, (int)solverData->info);
    success = 0;
  }
  else if(solverData->info > 0)
  {
    warningStreamPrint(LOG_STDOUT, 0,
        "Failed to solve linear system of equations (no. %d) at time %f, system is singular for U[%d, %d].",
        (int)systemData->equationIndex, data->localData[0]->timeValue, (int)solverData->info+1, (int)solverData->info+1);

    /* debug output */
    if (ACTIVE_STREAM(LOG_LS))
    {
      long int l = 0;
      long int k = 0;
      char buffer[4096];
      debugStreamPrint(LOG_LS, 0, "Matrix U:");
      for(l = 0; l < systemData->size; l++)
      {
        buffer[0] = 0;
        for(k = 0; k < systemData->size; k++)
          sprintf(buffer, "%s%10g ", buffer, systemData->A[l + k*systemData->size]);
        debugStreamPrint(LOG_LS, 0, "%s", buffer);
      }
      debugStreamPrint(LOG_LS, 0, "Solution x:");
      buffer[0] = 0;
      for(k = 0; k < systemData->size; k++)
        sprintf(buffer, "%s%10g ", buffer, systemData->b[k]);
      debugStreamPrint(LOG_LS, 0, "%s", buffer);
      debugStreamPrint(LOG_LS, 0, "Solution x:");
      buffer[0] = 0;
      for(k = 0; k < systemData->size; k++)
        sprintf(buffer, "%s%10g ", buffer, systemData->b[k]);
      debugStreamPrint(LOG_LS, 0, "%s", buffer);
    }

    success = 0;
  }

  /* take the solution */
  memcpy(systemData->x, systemData->b, systemData->size*(sizeof(modelica_real)));
  
  return success;
}
