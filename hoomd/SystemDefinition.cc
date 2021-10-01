// Copyright (c) 2009-2021 The Regents of the University of Michigan
// This file is part of the HOOMD-blue project, released under the BSD 3-Clause License.

// Maintainer: joaander

/*! \file SystemDefinition.cc
    \brief Defines SystemDefinition
*/

#include "SystemDefinition.h"

#include "SnapshotSystemData.h"

#ifdef ENABLE_MPI
#include "Communicator.h"
#endif

namespace py = pybind11;

using namespace std;

namespace hoomd {

/*! \post All shared pointers contained in SystemDefinition are NULL
 */
SystemDefinition::SystemDefinition() { }

/*! \param N Number of particles to allocate
    \param box Initial box particles are in
    \param n_types Number of particle types to set
    \param n_bond_types Number of bond types to create
    \param n_angle_types Number of angle types to create
    \param n_dihedral_types Number of dihedral types to create
    \param n_improper_types Number of improper types to create
    \param exec_conf The ExecutionConfiguration HOOMD is to be run on

    Creating SystemDefinition with this constructor results in
     - ParticleData constructed with the arguments \a N, \a box, \a n_types, and \a exec_conf->
     - BondData constructed with the arguments \a n_bond_types
     - All other data structures are default constructed.
*/
SystemDefinition::SystemDefinition(unsigned int N,
                                   const BoxDim& box,
                                   unsigned int n_types,
                                   unsigned int n_bond_types,
                                   unsigned int n_angle_types,
                                   unsigned int n_dihedral_types,
                                   unsigned int n_improper_types,
                                   std::shared_ptr<ExecutionConfiguration> exec_conf,
                                   std::shared_ptr<DomainDecomposition> decomposition)
    {
    m_n_dimensions = 3;
    m_particle_data = std::shared_ptr<ParticleData>(
        new ParticleData(N, box, n_types, exec_conf, decomposition));
    m_bond_data = std::shared_ptr<BondData>(new BondData(m_particle_data, n_bond_types));

    m_angle_data = std::shared_ptr<AngleData>(new AngleData(m_particle_data, n_angle_types));
    m_dihedral_data
        = std::shared_ptr<DihedralData>(new DihedralData(m_particle_data, n_dihedral_types));
    m_improper_data
        = std::shared_ptr<ImproperData>(new ImproperData(m_particle_data, n_improper_types));
    m_constraint_data = std::shared_ptr<ConstraintData>(new ConstraintData(m_particle_data, 0));
    m_pair_data = std::shared_ptr<PairData>(new PairData(m_particle_data, 0));
    m_integrator_data = std::shared_ptr<IntegratorData>(new IntegratorData());
    }

/*! Evaluates the snapshot and initializes the respective *Data classes using
   its contents (box dimensions and sub-snapshots)
    \param snapshot Snapshot to use
    \param exec_conf Execution configuration to run on
    \param decomposition (optional) The domain decomposition layout
*/
template<class Real>
SystemDefinition::SystemDefinition(std::shared_ptr<SnapshotSystemData<Real>> snapshot,
                                   std::shared_ptr<ExecutionConfiguration> exec_conf,
                                   std::shared_ptr<DomainDecomposition> decomposition)
    {
    setNDimensions(snapshot->dimensions);

    m_particle_data = std::shared_ptr<ParticleData>(
        new ParticleData(snapshot->particle_data, snapshot->global_box, exec_conf, decomposition));

#ifdef ENABLE_MPI
    // in MPI simulations, broadcast dimensionality from rank zero
    if (m_particle_data->getDomainDecomposition())
        bcast(m_n_dimensions, 0, exec_conf->getMPICommunicator());
#endif

    m_bond_data = std::shared_ptr<BondData>(new BondData(m_particle_data, snapshot->bond_data));

    m_angle_data = std::shared_ptr<AngleData>(new AngleData(m_particle_data, snapshot->angle_data));

    m_dihedral_data
        = std::shared_ptr<DihedralData>(new DihedralData(m_particle_data, snapshot->dihedral_data));

    m_improper_data
        = std::shared_ptr<ImproperData>(new ImproperData(m_particle_data, snapshot->improper_data));

    m_constraint_data = std::shared_ptr<ConstraintData>(
        new ConstraintData(m_particle_data, snapshot->constraint_data));
    m_pair_data = std::shared_ptr<PairData>(new PairData(m_particle_data, snapshot->pair_data));
    m_integrator_data = std::shared_ptr<IntegratorData>(new IntegratorData());
    }

/*! Sets the dimensionality of the system.  When quantities involving the dof of
    the system are computed, such as T, P, etc., the dimensionality is needed.
    Therefore, the dimensionality must be set before any temperature/pressure
    computes, thermostats/barostats, etc. are added to the system.
    \param n_dimensions Number of dimensions
*/
void SystemDefinition::setNDimensions(unsigned int n_dimensions)
    {
    if (!(n_dimensions == 2 || n_dimensions == 3))
        {
        m_particle_data->getExecConf()->msg->error()
            << "hoomd supports only 2D or 3D simulations" << endl;
        throw runtime_error("Error setting dimensions");
        }
    m_n_dimensions = n_dimensions;
    }

/*! \param particles True if particle data should be saved
 *  \param bonds True if bond data should be saved
 *  \param angles True if angle data should be saved
 *  \param dihedrals True if dihedral data should be saved
 *  \param impropers True if improper data should be saved
 *  \param constraints True if constraint data should be saved
 *  \param integrators True if integrator data should be saved
 *  \param pairs True if pair data should be saved
 */
template<class Real> std::shared_ptr<SnapshotSystemData<Real>> SystemDefinition::takeSnapshot()
    {
    std::shared_ptr<SnapshotSystemData<Real>> snap(new SnapshotSystemData<Real>);

    snap->dimensions = m_n_dimensions;
    snap->global_box = m_particle_data->getGlobalBox();

    snap->map = m_particle_data->takeSnapshot(snap->particle_data);
    m_bond_data->takeSnapshot(snap->bond_data);
    m_angle_data->takeSnapshot(snap->angle_data);
    m_dihedral_data->takeSnapshot(snap->dihedral_data);
    m_improper_data->takeSnapshot(snap->improper_data);
    m_constraint_data->takeSnapshot(snap->constraint_data);
    m_pair_data->takeSnapshot(snap->pair_data);

    return snap;
    }

//! Re-initialize the system from a snapshot
template<class Real>
void SystemDefinition::initializeFromSnapshot(std::shared_ptr<SnapshotSystemData<Real>> snapshot)
    {
    std::shared_ptr<const ExecutionConfiguration> exec_conf = m_particle_data->getExecConf();

    m_n_dimensions = snapshot->dimensions;

#ifdef ENABLE_MPI
    // in MPI simulations, broadcast dimensionality from rank zero
    if (m_particle_data->getDomainDecomposition())
        bcast(m_n_dimensions, 0, exec_conf->getMPICommunicator());
#endif

    m_particle_data->setGlobalBox(snapshot->global_box);
    m_particle_data->initializeFromSnapshot(snapshot->particle_data);
    m_bond_data->initializeFromSnapshot(snapshot->bond_data);
    m_angle_data->initializeFromSnapshot(snapshot->angle_data);
    m_dihedral_data->initializeFromSnapshot(snapshot->dihedral_data);
    m_improper_data->initializeFromSnapshot(snapshot->improper_data);
    m_constraint_data->initializeFromSnapshot(snapshot->constraint_data);
    m_pair_data->initializeFromSnapshot(snapshot->pair_data);
    }

// instantiate both float and double methods
template SystemDefinition::SystemDefinition(std::shared_ptr<SnapshotSystemData<float>> snapshot,
                                            std::shared_ptr<ExecutionConfiguration> exec_conf,
                                            std::shared_ptr<DomainDecomposition> decomposition);
template std::shared_ptr<SnapshotSystemData<float>> SystemDefinition::takeSnapshot<float>();
template void SystemDefinition::initializeFromSnapshot<float>(
    std::shared_ptr<SnapshotSystemData<float>> snapshot);

template SystemDefinition::SystemDefinition(std::shared_ptr<SnapshotSystemData<double>> snapshot,
                                            std::shared_ptr<ExecutionConfiguration> exec_conf,
                                            std::shared_ptr<DomainDecomposition> decomposition);
template std::shared_ptr<SnapshotSystemData<double>> SystemDefinition::takeSnapshot<double>();
template void SystemDefinition::initializeFromSnapshot<double>(
    std::shared_ptr<SnapshotSystemData<double>> snapshot);

namespace detail {

void export_SystemDefinition(py::module& m)
    {
    py::class_<SystemDefinition, std::shared_ptr<SystemDefinition>>(m, "SystemDefinition")
        .def(py::init<>())
        .def(py::init<unsigned int,
                      const BoxDim&,
                      unsigned int,
                      unsigned int,
                      unsigned int,
                      unsigned int,
                      unsigned int,
                      std::shared_ptr<ExecutionConfiguration>>())
        .def(py::init<unsigned int,
                      const BoxDim&,
                      unsigned int,
                      unsigned int,
                      unsigned int,
                      unsigned int,
                      unsigned int,
                      std::shared_ptr<ExecutionConfiguration>,
                      std::shared_ptr<DomainDecomposition>>())
        .def(py::init<std::shared_ptr<SnapshotSystemData<float>>,
                      std::shared_ptr<ExecutionConfiguration>,
                      std::shared_ptr<DomainDecomposition>>())
        .def(py::init<std::shared_ptr<SnapshotSystemData<float>>,
                      std::shared_ptr<ExecutionConfiguration>>())
        .def(py::init<std::shared_ptr<SnapshotSystemData<double>>,
                      std::shared_ptr<ExecutionConfiguration>,
                      std::shared_ptr<DomainDecomposition>>())
        .def(py::init<std::shared_ptr<SnapshotSystemData<double>>,
                      std::shared_ptr<ExecutionConfiguration>>())
        .def("setNDimensions", &SystemDefinition::setNDimensions)
        .def("getNDimensions", &SystemDefinition::getNDimensions)
        .def("getParticleData", &SystemDefinition::getParticleData)
        .def("getBondData", &SystemDefinition::getBondData)
        .def("getAngleData", &SystemDefinition::getAngleData)
        .def("getDihedralData", &SystemDefinition::getDihedralData)
        .def("getImproperData", &SystemDefinition::getImproperData)
        .def("getConstraintData", &SystemDefinition::getConstraintData)
        .def("getIntegratorData", &SystemDefinition::getIntegratorData)
        .def("getPairData", &SystemDefinition::getPairData)
        .def("takeSnapshot_float", &SystemDefinition::takeSnapshot<float>)
        .def("takeSnapshot_double", &SystemDefinition::takeSnapshot<double>)
        .def("initializeFromSnapshot", &SystemDefinition::initializeFromSnapshot<float>)
        .def("initializeFromSnapshot", &SystemDefinition::initializeFromSnapshot<double>)
        .def("getSeed", &SystemDefinition::getSeed)
        .def("setSeed", &SystemDefinition::setSeed)
#ifdef ENABLE_MPI
        .def("setCommunicator", &SystemDefinition::setCommunicator)
#endif
        ;
    }

} // end namespace detail

} // end namespace hoomd
