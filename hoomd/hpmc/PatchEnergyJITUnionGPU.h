#pragma once

#ifdef ENABLE_HIP

#include "EvaluatorUnionGPU.cuh"
#include "GPUEvalFactory.h"
#include "PatchEnergyJITUnion.h"
#include "hoomd/managed_allocator.h"

#include <vector>

namespace hoomd {
namespace hpmc {

//! Evaluate patch energies via runtime generated code, GPU version
class PYBIND11_EXPORT PatchEnergyJITUnionGPU : public PatchEnergyJITUnion
    {
    public:
    //! Constructor
    PatchEnergyJITUnionGPU(std::shared_ptr<SystemDefinition> sysdef,
                           std::shared_ptr<ExecutionConfiguration> exec_conf,
                           const std::string& cpu_code_iso,
                           const std::vector<std::string>& cpu_compiler_args,
                           Scalar r_cut_iso,
                           pybind11::array_t<float> param_array_isotropic,
                           const std::string& cpu_code_constituent,
                           Scalar r_cut_constituent,
                           pybind11::array_t<float> param_array_constituent,
                           const std::string& code,
                           const std::string& kernel_name,
                           const std::vector<std::string>& options,
                           const std::string& cuda_devrt_library_path,
                           unsigned int compute_arch)
        : PatchEnergyJITUnion(sysdef,
                              exec_conf,
                              cpu_code_iso,
                              cpu_compiler_args,
                              r_cut_iso,
                              param_array_isotropic,
                              cpu_code_constituent,
                              r_cut_constituent,
                              param_array_constituent),
          m_gpu_factory(exec_conf,
                        code,
                        kernel_name,
                        options,
                        cuda_devrt_library_path,
                        compute_arch),
          m_d_union_params(m_sysdef->getParticleData()->getNTypes(),
                           jit::union_params_t(),
                           hoomd::detail::managed_allocator<jit::union_params_t>(m_exec_conf->isCUDAEnabled()))
        {
        m_gpu_factory.setAlphaPtr(m_param_array.data());
        m_gpu_factory.setAlphaUnionPtr(m_param_array_constituent.data());
        m_gpu_factory.setUnionParamsPtr(m_d_union_params.data());
        m_gpu_factory.setRCutUnion(float(m_r_cut_constituent));

        // tuning params for patch narrow phase
        std::vector<unsigned int> valid_params_patch;
        const unsigned int narrow_phase_max_threads_per_eval = this->m_exec_conf->dev_prop.warpSize;
        auto& launch_bounds = m_gpu_factory.getLaunchBounds();
        for (auto cur_launch_bounds : launch_bounds)
            {
            for (unsigned int group_size = 1; group_size <= cur_launch_bounds; group_size *= 2)
                {
                for (unsigned int eval_threads = 1;
                     eval_threads <= narrow_phase_max_threads_per_eval;
                     eval_threads *= 2)
                    {
                    if ((cur_launch_bounds % (group_size * eval_threads)) == 0)
                        valid_params_patch.push_back(cur_launch_bounds * 1000000 + group_size * 100
                                                     + eval_threads);
                    }
                }
            }

        m_tuner_narrow_patch.reset(
            new Autotuner(valid_params_patch, 5, 100000, "hpmc_narrow_patch", this->m_exec_conf));

        m_managed_memory = true;
        }

    virtual ~PatchEnergyJITUnionGPU() { }

    virtual void buildOBBTree(unsigned int type_id)
        {
        PatchEnergyJITUnion::buildOBBTree(type_id);
        m_d_union_params[type_id].tree = m_tree[type_id];
        }

    //! Set per-type typeid of constituent particles
    virtual void setTypeids(std::string type, pybind11::list typeids)
        {
        unsigned int type_id = m_sysdef->getParticleData()->getTypeByName(type);
        unsigned int N = (unsigned int)pybind11::len(typeids);
        m_type[type_id].resize(N);
        ManagedArray<unsigned int> new_type_ids(N, true);

        for (unsigned int i = 0; i < N; i++)
            {
            unsigned int t = pybind11::cast<unsigned int>(typeids[i]);
            m_type[type_id][i] = t;
            new_type_ids[i] = t;
            }

        // store result
        m_d_union_params[type_id].mtype = new_type_ids;
        // cudaMemadviseReadMostly
        m_d_union_params[type_id].set_memory_hint();
        }

    //! Set per-type positions of the constituent particles
    virtual void setPositions(std::string type, pybind11::list position)
        {
        unsigned int type_id = m_sysdef->getParticleData()->getTypeByName(type);
        unsigned int N = (unsigned int)pybind11::len(position);
        m_position[type_id].resize(N);

        ManagedArray<vec3<float>> new_positions(N, true);

        for (unsigned int i = 0; i < N; i++)
            {
            pybind11::tuple p_i = position[i];
            vec3<float> pos(p_i[0].cast<float>(), p_i[1].cast<float>(), p_i[2].cast<float>());
            m_position[type_id][i] = pos;
            new_positions[i] = pos;
            }

        // store result
        m_d_union_params[type_id].mpos = new_positions;
        // cudaMemadviseReadMostly
        m_d_union_params[type_id].set_memory_hint();
        buildOBBTree(type_id);
        }

    //! Set per-type positions of the constituent particles
    virtual void setOrientations(std::string type, pybind11::list orientation)
        {
        unsigned int type_id = m_sysdef->getParticleData()->getTypeByName(type);
        unsigned int N = (unsigned int)pybind11::len(orientation);
        m_orientation[type_id].resize(N);

        ManagedArray<quat<float>> new_orientations(N, true);

        for (unsigned int i = 0; i < N; i++)
            {
            pybind11::tuple q_i = orientation[i];
            float s = q_i[0].cast<float>();
            float x = q_i[1].cast<float>();
            float y = q_i[2].cast<float>();
            float z = q_i[3].cast<float>();
            quat<float> ort(s, vec3<float>(x, y, z));
            m_orientation[type_id][i] = ort;
            new_orientations[i] = ort;
            }

        // store result
        m_d_union_params[type_id].morientation = new_orientations;
        // cudaMemadviseReadMostly
        m_d_union_params[type_id].set_memory_hint();
        }

    //! Set per-type diameters of the constituent particles
    virtual void setDiameters(std::string type, pybind11::list diameter)
        {
        unsigned int type_id = m_sysdef->getParticleData()->getTypeByName(type);
        unsigned int N = (unsigned int)pybind11::len(diameter);
        m_diameter[type_id].resize(N);

        ManagedArray<float> new_diameters(N, true);

        for (unsigned int i = 0; i < N; i++)
            {
            float d = diameter[i].cast<float>();
            m_diameter[type_id][i] = d;
            new_diameters[i] = d;
            }

        // store result
        m_d_union_params[type_id].mdiameter = new_diameters;
        // cudaMemadviseReadMostly
        m_d_union_params[type_id].set_memory_hint();
        }

    //! Set per-type charges of the constituent particles
    virtual void setCharges(std::string type, pybind11::list charge)
        {
        unsigned int type_id = m_sysdef->getParticleData()->getTypeByName(type);
        unsigned int N = (unsigned int)pybind11::len(charge);
        m_charge[type_id].resize(N);

        ManagedArray<float> new_charges(N, true);

        for (unsigned int i = 0; i < N; i++)
            {
            float q = charge[i].cast<float>();
            m_charge[type_id][i] = q;
            new_charges[i] = q;
            }

        // store result
        m_d_union_params[type_id].mcharge = new_charges;
        // cudaMemadviseReadMostly
        m_d_union_params[type_id].set_memory_hint();
        }

    //! Asynchronously launch the JIT kernel
    /*! \param args Kernel arguments
        \param hStream stream to execute on
        */
    virtual void computePatchEnergyGPU(const gpu_args_t& args, hipStream_t hStream);

    //! Set autotuner parameters
    /*! \param enable Enable/disable autotuning
        \param period period (approximate) in time steps when returning occurs
    */
    virtual void setAutotunerParams(bool enable, unsigned int period)
        {
        m_tuner_narrow_patch->setPeriod(period);
        m_tuner_narrow_patch->setEnabled(enable);
        }

    protected:
    std::unique_ptr<Autotuner> m_tuner_narrow_patch; //!< Autotuner for the narrow phase

    private:
    GPUEvalFactory m_gpu_factory; //!< JIT implementation

    std::vector<jit::union_params_t, hoomd::detail::managed_allocator<jit::union_params_t>>
        m_d_union_params; //!< Parameters for each particle type on GPU
    };

namespace detail {

//! Exports the PatchEnergyJITUnionGPU class to python
void export_PatchEnergyJITUnionGPU(pybind11::module& m);

} // end namespace detail
} // end namespace hpmc
} // end namespace hoomd
#endif
