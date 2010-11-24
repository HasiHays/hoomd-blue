/*
Highly Optimized Object-oriented Many-particle Dynamics -- Blue Edition
(HOOMD-blue) Open Source Software License Copyright 2008, 2009 Ames Laboratory
Iowa State University and The Regents of the University of Michigan All rights
reserved.

HOOMD-blue may contain modifications ("Contributions") provided, and to which
copyright is held, by various Contributors who have granted The Regents of the
University of Michigan the right to modify and/or distribute such Contributions.

Redistribution and use of HOOMD-blue, in source and binary forms, with or
without modification, are permitted, provided that the following conditions are
met:

* Redistributions of source code must retain the above copyright notice, this
list of conditions, and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice, this
list of conditions, and the following disclaimer in the documentation and/or
other materials provided with the distribution.

* Neither the name of the copyright holder nor the names of HOOMD-blue's
contributors may be used to endorse or promote products derived from this
software without specific prior written permission.

Disclaimer

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER AND CONTRIBUTORS ``AS IS''
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND/OR
ANY WARRANTIES THAT THIS SOFTWARE IS FREE OF INFRINGEMENT ARE DISCLAIMED.

IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

// $Id$
// $URL$
// Maintainer: joaander

/*! \file MOL2DumpWriter.cc
    \brief Defines the MOL2DumpWriter class
*/

#ifdef WIN32
#pragma warning( push )
#pragma warning( disable : 4244 )
#endif

#include <boost/python.hpp>
using namespace boost::python;

#include <iomanip>
#include <fstream>
#include <stdexcept>
#include <boost/shared_ptr.hpp>

#include "MOL2DumpWriter.h"
#include "BondData.h"

using namespace std;

/*! \param sysdef SystemDefinition containing the ParticleData to dump
    \param fname_base The base file name to write the output to
*/
MOL2DumpWriter::MOL2DumpWriter(boost::shared_ptr<SystemDefinition> sysdef, std::string fname_base)
        : Analyzer(sysdef), m_base_fname(fname_base)
    {
    }

/*! \param timestep Current time step of the simulation
    Writes a snapshot of the current state of the ParticleData to a mol2 file
*/
void MOL2DumpWriter::analyze(unsigned int timestep)
    {
    if (m_prof)
        m_prof->push("Dump MOL2");
    
    ostringstream full_fname;
    string filetype = ".mol2";
    
    // Generate a filename with the timestep padded to ten zeros
    full_fname << m_base_fname << "." << setfill('0') << setw(10) << timestep << filetype;
    writeFile(full_fname.str());

    if (m_prof)
        m_prof->pop();
    }

/*! \param fname File name to write
*/
void MOL2DumpWriter::writeFile(std::string fname)
    {
    // open the file for writing
    ofstream f(fname.c_str());
    
    if (!f.good())
        {
        cerr << endl << "***Error! Unable to open dump file for writing: " << fname << endl << endl;
        throw runtime_error("Error writting mol2 dump file");
        }
        
    // acquire the particle data
    ParticleDataArraysConst arrays = m_pdata->acquireReadOnly();
    
    // write the header
    f << "@<TRIPOS>MOLECULE" <<endl;
    f << "Generated by HOOMD" << endl;
    int num_bonds = 1;
    boost::shared_ptr<BondData> bond_data = m_sysdef->getBondData();
    if (bond_data && bond_data->getNumBonds() > 0)
        num_bonds = bond_data->getNumBonds();
        
    f << m_pdata->getN() << " " << num_bonds << endl;
    f << "NO_CHARGES" << endl;
    
    f << "@<TRIPOS>ATOM" << endl;
    for (unsigned int j = 0; j < arrays.nparticles; j++)
        {
        // use the rtag data to output the particles in the order they were read in
        int i;
        i= arrays.rtag[j];
        
        // get the coordinates
        Scalar x = (arrays.x[i]);
        Scalar y = (arrays.y[i]);
        Scalar z = (arrays.z[i]);
        
        // get the type by name
        unsigned int type_id = arrays.type[i];
        string type_name = m_pdata->getNameByType(type_id);
        
        // this is intended to go to VMD, so limit the type name to 15 characters
        if (type_name.size() > 15)
            {
            cerr << endl << "Error! Type name <" << type_name << "> too long: please limit to 15 characters" 
                 << endl << endl;
            throw runtime_error("Error writting mol2 dump file");
            }
            
        f << j+1 << " " << type_name << " " << x << " " << y << " "<< z << " " << type_name << endl;
        
        if (!f.good())
            {
            cerr << endl << "***Error! Unexpected error writing MOL2 dump file" << endl << endl;
            throw runtime_error("Error writting mol2 dump file");
            }
        }
        
    // write a dummy bond since VMD doesn't like loading mol2 files without bonds
    // f << "@<TRIPOS>BOND" << endl;
    // f << "1 1 2 1" << endl;
    
    f << "@<TRIPOS>BOND" << endl;
    if (bond_data && bond_data->getNumBonds() > 0)
        {
        for (unsigned int i = 0; i < bond_data->getNumBonds(); i++)
            {
            Bond b = bond_data->getBond(i);
            f << i+1 << " " << b.a+1 << " " << b.b+1 << " 1" << endl;
            }
        }
    else
        {
        f << "1 1 2 1" << endl;
        }
        
    if (!f.good())
        {
        cerr << endl << "***Error! Unexpected error writing HOOMD dump file" << endl << endl;
        throw runtime_error("Error writting mol2 dump file");
        }
        
    f.close();
    m_pdata->release();
    }

void export_MOL2DumpWriter()
    {
    class_<MOL2DumpWriter, boost::shared_ptr<MOL2DumpWriter>, bases<Analyzer>, boost::noncopyable>
    ("MOL2DumpWriter", init< boost::shared_ptr<SystemDefinition>, std::string >())
    .def("writeFile", &MOL2DumpWriter::writeFile)
    ;
    
    }

#ifdef WIN32
#pragma warning( pop )
#endif

