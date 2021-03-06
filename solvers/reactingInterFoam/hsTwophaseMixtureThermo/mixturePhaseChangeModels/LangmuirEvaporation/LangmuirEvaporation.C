/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     |
    \\  /    A nd           | Copyright (C) 2011 OpenFOAM Foundation
     \\/     M anipulation  |
-------------------------------------------------------------------------------
License
    This file is part of OpenFOAM.

    OpenFOAM is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    OpenFOAM is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
    for more details.

    You should have received a copy of the GNU General Public License
    along with OpenFOAM.  If not, see <http://www.gnu.org/licenses/>.

\*---------------------------------------------------------------------------*/

#include "LangmuirEvaporation.H"
#include "addToRunTimeSelectionTable.H"

// * * * * * * * * * * * * * * Static Data Members * * * * * * * * * * * * * //

namespace Foam
{
namespace mixturePhaseChangeModels
{
    defineTypeNameAndDebug(LangmuirEvaporation, 0);
    addToRunTimeSelectionTable(mixturePhaseChangeModel, LangmuirEvaporation, components);
}
}

// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //
Foam::mixturePhaseChangeModels::LangmuirEvaporation::LangmuirEvaporation
(
    const word name,
    const fvMesh& mesh,
    const phase& alphaL,
    const phase& alphaV,
    const PtrList<gasThermoPhysics>& speciesData,
    dictionary phaseChangeDict
)
:
    mixturePhaseChangeModel
    (
        typeName, 
        name, 
        mesh, 
        alphaL, 
        alphaV, 
        speciesData, 
        phaseChangeDict
    ),
    x_
    (
        IOobject
        (
            "x_" + name_,
            mesh_.time().timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::AUTO_WRITE
        ),
        mesh_,
        dimensionedScalar("x",dimless,0.0)
    ),
    xL_
    (
        IOobject
        (
            "xL_" + name_,
            mesh_.time().timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::AUTO_WRITE
        ),
        mesh_,
        dimensionedScalar("xL",dimless,0.0)
    ),
    coeffC_
    (
        IOobject
        (
            "coeffC_" + name_,
            mesh_.time().timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::NO_WRITE
        ),
        mesh_,
        dimensionedScalar("coeffC",dimTime/dimArea,0.0)
    ),
    coeffV_
    (
        IOobject
        (
            "coeffV_" + name_,
            mesh_.time().timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::NO_WRITE
        ),
        mesh_,
        dimensionedScalar("coeffV",dimTime/dimArea,0.0)
    ),
    p_vap_
    (
        IOobject
        (
            "p_vap_" + name_,
            mesh_.time().timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::NO_WRITE
        ),
        mesh_,
        dimensionedScalar("pv",dimPressure,0.0)
    ),
    Pc_(phaseChangeDict_.lookup("Pc")),
    Tc_(phaseChangeDict_.lookup("Tc")),
    Tb_(phaseChangeDict_.lookup("Tb")),
    Lb_(phaseChangeDict_.lookup("Lb")),
    La_(readScalar(phaseChangeDict_.lookup("La"))),
    PvCoeffs_(phaseChangeDict_.lookup("PvCoeffs")),
    betaV_(readScalar(phaseChangeDict_.lookup("betaV"))),
    betaC_(readScalar(phaseChangeDict_.lookup("betaC"))),
    W_(dimensionedScalar("W", dimMass/dimMoles, 0.0)) // kg/kmol
{    
    List<word> prodList = products_.toc();
    List<word> reacList = reactants_.toc();
    
    //TODO: Warn if length of reactants > 1
    
    
    
    vapor_specie_ = prodList[0];
    liquid_specie_ = reacList[0];
    
    //W_ = alphaV_.subSpecies()[vapor_specie_]->W();
    W_.value() = reacThermo_[liquid_specie_]->W();
    
    Info<< "Liquid/Vapor evaporation configured for " << liquid_specie_ << endl;
}
    
// * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * * //
void Foam::mixturePhaseChangeModels::LangmuirEvaporation::calculate
(
    const volScalarField& phaseChangeZones,
    const volScalarField& area
)
{
    //Calculate the vapor pressure
    //dimensionedScalar pZero("pZ",dimPressure,0);
    dimensionedScalar p0("p0",dimPressure,101325);
    
    /*p_vap_ = pZero;
    
    forAll(p_vap_, cellI)
    {
        if( area[cellI] > SMALL )
        {
            const scalar& Ti = T_[cellI];
            if( Ti < Tb_.value() )
            {
                p_vap_[cellI] = 101325*exp
                (
                    -Lb_.value()/R_.value()*(1.0/Ti - 1.0/Tb_.value())
                );
            }
            else if( Ti < Tc_.value()
            {
                p_vap_[cellI] = exp
                (
                   PvCoeffs_[0]
                 - PvCoeffs_[1]/Ti
                 - PvCoeffs_[2]/(Ti*Ti)
                );
            }
            else
            {
                p_vap_[cellI] = Pc_.value();
            }
        }
    }*/
    
    p_vap_ = p0*exp
     (
        (-Lb_/R_*(1.0/T_ - 1.0/Tb_))*neg(T_ - Tb_)

      + (
           PvCoeffs_[0]
         - dimensionedScalar("B",dimTemperature,PvCoeffs_[1])/T_
         - dimensionedScalar("C",dimTemperature*dimTemperature,PvCoeffs_[2])
         / (T_*T_)
        )*pos(T_ - Tb_)
     );
    
    p_vap_.min(Pc_);
    
    dimensionedScalar sA("sA",dimless/dimLength,SMALL);
    
    // Allow new phase nucleation only on interfaces, and supress where
    // non-boiling interfaces overlap
    mask_ = (phaseChangeZones * neg(T_ - 0.9*Tc_) +  pos(T_ - 0.9*Tc_)) 
          * pos(area-sA);
                          
    //Calculate the mole fractions
    /*volScalarField xL_avg = alphaL_.x(liquid_specie_);
    dimensionedScalar dA = pow(min(mesh_.V()),2.0/3.0)/25.0;
    
    for( label i = 0; i < 20; ++i )
    {
        xL_avg += fvc::laplacian(dA*alphaL_.faceMask(), xL_avg);
    }
    xL_avg.min(1.0);
    xL_avg.max(0.0);
    xL_avg.correctBoundaryConditions();

    xL_ = alphaL_.x(liquid_specie_)*pos(alphaL_.Yp() - 1e-2)
          + xL_avg * neg(alphaL_.Yp() - 1e-2);*/
          
    
    xL_ = alphaL_.x(liquid_specie_);

    
    // use successive averaging
    surfaceScalarField xLf = fvc::interpolate(xL_);
    
    volScalarField wgts = 0.01*alphaL_.cellMask()*alphaV_.cellMask();
    
    for( label i = 0; i < 5; ++i )
    {
        xL_ = (1-wgts)*xL_ + wgts*fvc::average(xLf);
        xLf = fvc::interpolate(xL_);
    }
    
    
    xL_.correctBoundaryConditions();
    
    Info<<"Max xL_"<<liquid_specie_<<" = " << Foam::max(xL_).value() << endl;
    
    tmp<volScalarField> xSat = p_vap_/p_*xL_;
    xSat().min(1.0);
    
    //set x to xSat in areas with trace mass fractions
    x_ = alphaV_.x(vapor_specie_)*pos(alphaV_.Yp() - 1e-4) 
         + xSat*neg(alphaV_.Yp() - 1e-4);

    
    /*x_ *= 0.0;
    List<word> prodList = products_.toc();
    forAll(prodList, p)
    {
        //TODO: Wrong with multiple product species!
        x_ += (alphaV_.x(prodList[p])*pos(alphaV_.Yp() - 1e-4) 
              + xSat*neg(alphaV_.Yp() - 1e-4))*products_[prodList[p]];
    }*/
    
    
    scalar pi = constant::mathematical::pi;

    tmp<volScalarField> coeff = area*Foam::sqrt(W_/(2*pi*R_*T_))*mask_;
    
    dimensionedScalar sp("sp",dimPressure,1e-2);
    coeffC_ = 2.0*betaC_/(2.0-betaC_)*coeff()*neg(p_vap_*xL_ + sp - p_*x_);
    coeffV_ = 2.0*betaV_/(2.0-betaV_)*coeff()*pos(p_vap_*xL_ - sp - p_*x_);
    
    omega_ = (coeffC_ + coeffV_) * (p_vap_*xL_ - p_*x_) / W_;

    Foam::Info<< "Min,max evaporation flux for " << liquid_specie_ << " = "
              << Foam::min(omega_).value() << ", " 
              << Foam::max(omega_).value() << " kmol/m3/s" << Foam::endl;
}

tmp<volScalarField> Foam::mixturePhaseChangeModels::LangmuirEvaporation::mdot
(
    const word& phaseName
) const
{
    tmp<volScalarField> tmdot
    (
        new volScalarField
        (
            IOobject
            (
                "tmdot",
                mesh_.time().timeName(),
                mesh_
            ),
            mesh_,
            dimensionedScalar("tmdot",dimDensity/dimTime,0.0)
        )
    );
    
    if( phaseName == "Liquid" )
    {
        tmdot() -= omega_*W_;
    }
    else if( phaseName == "Vapor" )
    {
        tmdot() += omega_*W_;
    }
    else
    {
        Info<<"WARNING: Invalid phase " << phaseName << endl;
    }
    
    return tmdot;
}

tmp<volScalarField> Foam::mixturePhaseChangeModels::LangmuirEvaporation::Vdot
(
    const word& phaseName
) const
{
    tmp<volScalarField> tVdot
    (
        new volScalarField
        (
            IOobject
            (
                "tVdot",
                mesh_.time().timeName(),
                mesh_
            ),
            mesh_,
            dimensionedScalar("tVdot",dimless/dimTime,0.0)
        )
    );
    
    if( phaseName == "Liquid" )
    {
        dimensionedScalar rhoL0 = alphaL_.subSpecies()[liquid_specie_]->rho0();
        tVdot() -= omega_*W_/rhoL0;
    }
    else if( phaseName == "Vapor" )
    {
        tVdot() += omega_*R_*T_/p_;
        
        /*scalar sumF = 0.0;
        
        forAllConstIter(HashTable<scalar>, products_, fpI)
        {
            sumF += fpI();
        }
        
        tVdot() += omega_*R_*T_/p_ * sumF;*/
    }
    else
    {
        Info<<"WARNING: Invalid phase " << phaseName << endl;
    }
    
    return tVdot;
}


// get the explicit and implicit source terms for Yvapor
Pair<tmp<volScalarField> > Foam::mixturePhaseChangeModels::LangmuirEvaporation::YSuSp
(
    const word& specie
) const
{
    Pair<tmp<volScalarField> > tYSuSp
    (
        tmp<volScalarField>
        (
            new volScalarField
            (
                IOobject
                (
                    "tYSu",
                    mesh_.time().timeName(),
                    mesh_
                ),
                mesh_,
                dimensionedScalar("YSu", dimDensity/dimTime, 0.0)
            )
        ),
        tmp<volScalarField>
        (
            new volScalarField
            (
                IOobject
                (
                    "tYSp",
                    mesh_.time().timeName(),
                    mesh_
                ),
                mesh_,
                dimensionedScalar("YSp", dimDensity/dimTime, 0.0)
            )
        )
    );
    
    
    if( specie == vapor_specie_ )
    {
        //S_Yv = explicit - implicit*Yv
        // This casts the evaporation in the form C*(Ysat - Y)
         
        tmp<volScalarField> xByY = alphaV_.xByY(vapor_specie_);
        tYSuSp.first() = (coeffC_+coeffV_)*p_vap_*xL_;
        tYSuSp.second() = (coeffC_+coeffV_)*xByY*p_;
    }
    
    else if( specie == liquid_specie_ )
    {
        //S_YL = explicit
        //tYSuSp.first() = -(coeffC_+coeffV_)*(p_vap_*xL_ - x_*p_);
        tYSuSp.first() = -omega_*W_;
    }
    /*else if( products_.found(specie) )
    {
        scalar Wp = prodThermo_[specie]->W()/W_.value() * products_[specie];
        
        tmp<volScalarField> xByY = alphaV_.xByY(specie);
        tYSuSp.first() = (coeffC_+coeffV_)*p_vap_*xL_ * Wp;
        tYSuSp.second() = (coeffC_+coeffV_)*xByY*p_ * Wp;
    }*/

    return tYSuSp;
}



tmp<volScalarField> Foam::mixturePhaseChangeModels::LangmuirEvaporation::dPvdT() const
{
    tmp<volScalarField> tdPvdT
    (
        new volScalarField
        (
            IOobject
            (
                "tdPvdT",
                mesh_.time().timeName(),
                mesh_
            ),
            mesh_,
            dimensionedScalar("dPdT",dimPressure/dimTemperature,0.0)
        )
    );

    tdPvdT() = p_vap_/T_/T_*
       (
           Lb_/R_*neg(T_ - Tb_)
         + (
               dimensionedScalar("B",dimTemperature,PvCoeffs_[1])
             + 2*dimensionedScalar("C",dimTemperature*dimTemperature,PvCoeffs_[2])/T_
           )*pos(T_ - Tb_)
       );


    return tdPvdT * neg(p_vap_ - Pc_);
}

Pair<tmp<volScalarField> > Foam::mixturePhaseChangeModels::LangmuirEvaporation::TSuSp() const
{

    //Sh = -omega_*L()
    //
    //Sh = explicit - implicit * T
    //
    //  implicit = -dSh/dT = (dm/dT*L() + m_evap*dL/dT)*area_
    //    dm/dT = dcoeff/dT*(pv*xL-p*x) + coeff*xL*dpv/dT
    //    dcoeff/dT = -coeff/(2*T)
    //    dpv/dT = pv * L / (R*T^2)

    tmp<volScalarField> tL = L();
    tmp<volScalarField> dodT = -omega_/(2*T_) + (coeffC_+coeffV_)/W_*xL_*dPvdT();
    tmp<volScalarField> Sp = dodT*tL() + omega_*dLdT();
    tmp<volScalarField> Su = -omega_*tL() + Sp()*T_;
    
    return Pair<tmp<volScalarField> >
    (
        Su,
        Sp
    );

}

tmp<volScalarField> Foam::mixturePhaseChangeModels::LangmuirEvaporation::L() const
{
    tmp<volScalarField> tL
    (
        new volScalarField
        (
            IOobject
            (
                "tL",
                mesh_.time().timeName(),
                mesh_
            ),
            mesh_,
            Lb_ 
        )
    );
        
    if( La_ > 0.0 )
    {
        tL() *= Foam::pow(Foam::mag(Tc_ - T_)/(Tc_ - Tb_),La_)*pos(Tc_ - T_);
    }
    
    return tL;
}

tmp<volScalarField> Foam::mixturePhaseChangeModels::LangmuirEvaporation::dLdT() const
{
    tmp<volScalarField> tdLdT
    (
        new volScalarField
        (
            IOobject
            (
                "tdLdT",
                mesh_.time().timeName(),
                mesh_
            ),
            mesh_,
            dimensionedScalar("dLdT",dimEnergy/dimMoles/dimTemperature,0.0)
        )
    );
        
    if( La_ > 0.0 )
    {
        tdLdT() = -Lb_*La_/(Tc_-Tb_)*
            Foam::pow(Foam::mag(Tc_ - T_)/(Tc_ - Tb_),La_-1.0)*pos(Tc_ - T_);
    }
    
    return tdLdT;
}

// ************************************************************************* //
