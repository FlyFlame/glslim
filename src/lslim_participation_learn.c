/******************************************************************************/
/*! \file lslim_participation_learn.c

\brief This file contains the functions for computing the user refinement used 
       by LSLIM.
  
*//****************************************************************************/

#include<slim.h>


/**********************************************************************/
/*! \brief A function which learns the new (better) clustering assignment,
  used by LSLIM.
  
  \param[in]  ctrl              Control structure
  \param[in]  train             Training data
  \param[in]  model             Model File
  \param[in]  participation     The vector of the old clustering assignment.
  \param[out] indifference      The vector specifying for every user who 
                                remained in the same cluster, whether this 
				happened because there is no differnce in the 
			        training error (value 1), or because there is no
				cluster for which the training error is smaller 
				(value 0).

  \return total_participation   The vector of the new clustering assignment.
 */
/**********************************************************************/
int *lslim_learn_pu_all(ctrl_t * ctrl, gk_csr_t * train, gk_csr_t * model,
		  int * participation, int **indifference)
{

  int u, assignment, datasize, step, startu, endu, i;
  int *total_participation = NULL;
  int *total_indiff = NULL;
  int indiff = 0;
  int *partial_participation, *partial_indiff;
  int *sizes = NULL;
  int *displays = NULL;

  /* Set up MPI */
 
  datasize = train->nrows;
  step = (datasize / ctrl->num_procs) +
      (ctrl->id < (datasize % ctrl->num_procs) ? 1 : 0);
  startu = ((datasize / ctrl->num_procs) * ctrl->id) +
      gk_min(ctrl->id, datasize % ctrl->num_procs);
  endu = startu + step;

  if ((endu < datasize) && (ctrl->id == ctrl->num_procs - 1)) {
    endu = datasize;
    step = datasize - startu;
  }

  if ((ctrl->size) * (ctrl->num_clusters) > model->ncols)
    model->ncols = (ctrl->size) * (ctrl->num_clusters);
  gk_csr_CreateIndex(model, GK_CSR_COL);

  /* For the set of users in this node, compute their best clustering
     assignment and the corresponding weights. */
  partial_participation = gk_imalloc(step, "malloc new g");
  partial_indiff = gk_imalloc(step, "malloc indifference vector");

  for (u = startu; u < endu; u++) {
    assignment = lslim_learn_pu(ctrl, train, u, model, participation[u], &indiff);
    partial_participation[u - startu] = assignment;
    partial_indiff[u - startu] = indiff;
  }

  MPI_Barrier(MPI_COMM_WORLD);

  /* Combine the clustering assignments and the weights of the users
     of different nodes to the total ones on the main node 0. */
  if (ctrl->id == 0) {
    sizes = gk_ismalloc(ctrl->num_procs, 0, "malloc sizes");
    displays = gk_ismalloc(ctrl->num_procs, 0, "malloc displays");
  }

  MPI_Gather(&step, 1, MPI_INT, sizes, 1, MPI_INT, 0, MPI_COMM_WORLD);

  if (ctrl->id == 0) {
    displays[0] = 0;
    for (i = 0; i < ctrl->num_procs; i++) {
      if (i != ctrl->num_procs - 1)
	displays[i + 1] = displays[i] + sizes[i];
    }
  }

  total_participation = gk_imalloc(train->nrows, "malloc participation");
  total_indiff = gk_imalloc(train->nrows, "malloc indifference vector");

  MPI_Gatherv(partial_participation, step, MPI_INT, total_participation,
	      sizes, displays, MPI_INT, 0, MPI_COMM_WORLD);

  MPI_Gatherv(partial_indiff, step, MPI_INT, total_indiff,
	      sizes, displays, MPI_INT, 0, MPI_COMM_WORLD);

  /* Broadcast the total clustering assignment and weight vector
     from node 0 to al the nodes */
  MPI_Bcast(total_participation, train->nrows, MPI_INT, 0, MPI_COMM_WORLD);
  MPI_Bcast(total_indiff, train->nrows, MPI_INT, 0, MPI_COMM_WORLD);
  
  /* Freeing */

  gk_free((void **) &partial_participation, (void **)&partial_indiff, LTERM);
  if (ctrl->id == 0) {
    gk_free((void **) &sizes, &displays, LTERM);
  }

  *indifference = total_indiff;

  return total_participation;
}

/**********************************************************************/
/*! \brief Learning the new clustering assignment of the user, in order
           to minimize the training error.

  \param[in]   ctrl              Control structure
  \param[in]   train             Training data
  \param[in]   u                 User u
  \param[in]   model             Model File
  \param[in]   participation     The old assignment of user u.
  \param[out]  indiff            The variable indicating in the case user u 
                                 remained in the same cluster, whether this 
				 happened because there is no difference in the
                                 training error (value 1), or because there is 
				 no cluster for which the training
                                 error is smaller (value 0).
  
  \return      assignment        The new assignment of user u.

 */
/**********************************************************************/
int lslim_learn_pu(ctrl_t * ctrl, gk_csr_t * train, int u, gk_csr_t * model,
	     int participation, int *indiff)
{

  int new_assignment;
  int cluster_id;
  double *error;
  double min_error;

  error = gk_dsmalloc(ctrl->num_clusters, 0, "malloc error");

  /* For every possible cluster, compute the training error */
  for (cluster_id = 0; cluster_id < ctrl->num_clusters; cluster_id++) {
    error[cluster_id] =
	lslim_training_error(ctrl, model, train, u, cluster_id);
  }

  /*The cluster for which this user has the smallest training error 
    is the cluster to which the user is assigned. */
  min_error = error[participation];
  new_assignment = participation;

  for (cluster_id = 0; cluster_id < ctrl->num_clusters; cluster_id++) {
    if (error[cluster_id] < min_error) {
      new_assignment = cluster_id;
      min_error = error[cluster_id];
    }   
  }
  
  *indiff = 0;

  if(new_assignment == participation){
    for (cluster_id = 0; cluster_id < ctrl->num_clusters; cluster_id++) {
      if((cluster_id != participation) && (error[cluster_id] == min_error)){
	*indiff = 1;
	//	printf("For user %d, it did not matter between clusters %d and 
	//%d\n", u, cluster_id, participation);
      }
    }
  } 
  

  /* Freeing */
  gk_free((void **) &error, LTERM);
  return new_assignment;
}
