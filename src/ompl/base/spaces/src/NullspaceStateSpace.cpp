/*********************************************************************
* Software License Agreement (BSD License)
*
*  Copyright (c) 2017, Rice University
*  All rights reserved.
*
*  Redistribution and use in source and binary forms, with or without
*  modification, are permitted provided that the following conditions
*  are met:
*
*   * Redistributions of source code must retain the above copyright
*     notice, this list of conditions and the following disclaimer.
*   * Redistributions in binary form must reproduce the above
*     copyright notice, this list of conditions and the following
*     disclaimer in the documentation and/or other materials provided
*     with the distribution.
*   * Neither the name of the Rice University nor the names of its
*     contributors may be used to endorse or promote products derived
*     from this software without specific prior written permission.
*
*  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
*  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
*  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
*  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
*  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
*  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
*  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
*  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
*  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
*  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
*  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
*  POSSIBILITY OF SUCH DAMAGE.
*********************************************************************/

/* Author: Zachary Kingston */

#include "ompl/base/spaces/NullspaceStateSpace.h"

#include "ompl/base/PlannerDataGraph.h"
#include "ompl/base/SpaceInformation.h"
#include "ompl/util/Exception.h"

#include <boost/graph/iteration_macros.hpp>

#include <eigen3/Eigen/Core>
#include <eigen3/Eigen/Geometry>

/// NullspaceStateSpace

/// Public

void ompl::base::NullspaceStateSpace::checkSpace(const SpaceInformation *si)
{
    if (dynamic_cast<NullspaceStateSpace *>(si->getStateSpace().get()) == nullptr)
        throw ompl::Exception("ompl::base::NullspaceStateSpace(): "
                              "si needs to use an NullspaceStateSpace!");
}

bool ompl::base::NullspaceStateSpace::traverseManifold(const State *from, const State *to, const bool interpolate,
                                                       std::vector<State *> *stateList) const
{
    // number of discrete steps between a and b in the state space
    int n = validSegmentCount(from, to);

    // Save a copy of the from state.
    if (stateList != nullptr)
    {
        stateList->clear();
        stateList->push_back(si_->cloneState(from)->as<State>());
    }

    if (n == 0)  // don't divide by zero
        return true;

    if (!constraint_->isSatisfied(from))
    {
        // This happens too many times so just ignore it
        // OMPL_DEBUG("ompl::base::ProjectedStateSpace::traverseManifold(): "
        //            "'from' state not valid!");
        return false;
    }

    const StateValidityCheckerPtr &svc = si_->getStateValidityChecker();
    double dist = distance(from, to);

    StateType *previous = cloneState(from)->as<StateType>();
    StateType *scratch = allocState()->as<StateType>();

    Eigen::VectorXd f(n_ - k_);
    Eigen::MatrixXd j(n_ - k_, n_);

    Eigen::VectorXd toV = to->as<StateType>()->constVectorView();

    bool there = false;
    while (!(there = dist < (delta_ + std::numeric_limits<double>::epsilon())))
    {
        // Compute the parameterization for interpolation
        double t = delta_ / dist;
        RealVectorStateSpace::interpolate(previous, to, t, scratch);

        Eigen::Map<const Eigen::VectorXd> pV = previous->constVectorView();

        constraint_->function(pV, f);
        constraint_->jacobian(pV, j);

        Eigen::FullPivLU<Eigen::MatrixXd> lu = j.fullPivLu();
        scratch->vectorView() = pV - lu.solve(f) + lu.kernel().rowwise().reverse() * (scratch->constVectorView() - pV);

        // Make sure the new state is valid, or we don't care as we are simply interpolating
        bool valid = interpolate || svc->isValid(scratch);

        // Check if we have deviated too far from our previous state
        bool deviated = distance(previous, scratch) > 2.0 * delta_;

        if (!valid || deviated)
            break;

        // Store the new state
        if (stateList != nullptr)
            stateList->push_back(si_->cloneState(scratch)->as<State>());

        // Check for divergence. Divergence is declared if we are no closer than
        // before projection
        double newDist = distance(scratch, to);
        if (newDist >= dist)
            break;

        dist = newDist;
        copyState(previous, scratch);
    }

    if (there && (stateList != nullptr))
        stateList->push_back(si_->cloneState(to)->as<State>());

    freeState(scratch);
    freeState(previous);
    return there;
}
