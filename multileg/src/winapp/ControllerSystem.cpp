#include "ControllerSystem.h"

#include <ppl.h>
#include <ToString.h>
#include <DebugPrint.h>
#include <MathHelp.h>
#include <btBulletDynamicsCommon.h>
#include "ConstraintComponent.h"


void ControllerSystem::removed(artemis::Entity &e)
{

}

void ControllerSystem::added(artemis::Entity &e)
{
	ControllerComponent* controller = controllerComponentMapper.get(e);

	m_controllersToBuild.push_back(controller);
}

void ControllerSystem::processEntity(artemis::Entity &e)
{

}

void ControllerSystem::update(float p_dt)
{
	//DEBUGPRINT(( (std::string("\nController start DT=") + toString(p_dt) + "\n").c_str() ));
	m_runTime += p_dt;

	// Update all transforms
	for (int i = 0; i < m_jointRigidBodies.size(); i++)
	{
		saveJointMatrix(i);
		m_jointTorques[i] = glm::vec3(0.0f);
	}
	int controllerCount = m_controllers.size();
	if (m_controllers.size()>0)
	{
		// Start with making the controllers parallel only.
		// They still write to a global torque list, but without collisions.
#ifndef MULTI
		// Single threaded implementation
		for (int n = 0; n < controllerCount; n++)
		{
			ControllerComponent* controller = m_controllers[n];
			ControllerComponent::Chain* legChain = &controller->m_DOFChain;
			// Run controller code here
			controllerUpdate(n, p_dt);
			for (unsigned int i = 0; i < legChain->getSize(); i++)
			{
				unsigned int tIdx = legChain->jointIDXChain[i];
				glm::vec3 torqueBase = legChain->DOFChain[i];
				glm::quat rot = glm::quat(torqueBase)*glm::quat(m_jointWorldTransforms[tIdx]);
				m_jointTorques[tIdx] += torqueBase*13.0f/**(float)(TORAD)*/;
			}
		}
#else
		// Multi threaded CPU implementation
		//concurrency::combinable<glm::vec3> sumtorques;
		concurrency::parallel_for(0, controllerCount, [&](int n) {
			ControllerComponent* controller = m_controllers[n];
			ControllerComponent::Chain* legChain = &controller->m_DOFChain;
			// Run controller code here
			controllerUpdate(n, p_dt);
			for (unsigned int i = 0; i < legChain->getSize(); i++)
			{
				unsigned int tIdx = legChain->jointIDXChain[i];
				glm::vec3 torqueBase = legChain->DOFChain[i];
				glm::quat rot = glm::quat(torqueBase)*glm::quat(m_jointWorldTransforms[tIdx]);
				m_jointTorques[tIdx] += torqueBase*13.0f/**(float)(TORAD)*/;
			}
		});
		/*concurrency::parallel_for(0, (int)legChain->getSize(), [&](int i) {
			unsigned int tIdx = legChain->jointIDXChain[i];
			glm::vec3 torqueBase = legChain->DOFChain[i];
			glm::quat rot = glm::quat(torqueBase)*glm::quat(m_jointWorldTransforms[tIdx]);
			m_jointTorques[tIdx] += torqueBase*13.0f;
		});
	*/

#endif

	}
}

void ControllerSystem::finish()
{

}

void ControllerSystem::applyTorques()
{
	if (m_jointRigidBodies.size() == m_jointTorques.size())
	{
		for (int i = 0; i < m_jointRigidBodies.size(); i++)
		{
			glm::vec3* t = &m_jointTorques[i];
			m_jointRigidBodies[i]->applyTorque(btVector3(t->x, t->y, t->z));
		}
	}
}

void ControllerSystem::buildCheck()
{
	for (int i = 0; i < m_controllersToBuild.size(); i++)
	{
		ControllerComponent* controller = m_controllersToBuild[i];
		ControllerComponent::LegFrameEntityConstruct* legFrameEntities = controller->getLegFrameEntityConstruct(0);
		ControllerComponent::LegFrame* legFrame = controller->getLegFrame(0);
		ControllerComponent::Chain* legChain = &controller->m_DOFChain;
		// Build the controller (Temporary code)
		// The below should be done for each leg (even the root)
		// Create ROOT
		RigidBodyComponent* rootRB = (RigidBodyComponent*)legFrameEntities->m_legFrameEntity->getComponent<RigidBodyComponent>();
		TransformComponent* rootTransform = (TransformComponent*)legFrameEntities->m_legFrameEntity->getComponent<TransformComponent>();
		unsigned int rootIdx = addJoint(rootRB, rootTransform);
		legFrame->m_legFrameJointId = rootIdx; // store idx to root for leg frame
		glm::vec3 DOF;
		for (int n = 0; n < 3; n++)
		{
			legChain->jointIDXChain.push_back(rootIdx);
			legChain->DOFChain.push_back(DOFAxisByVecCompId(n)); // root has 3DOF (for now, to not over-optimize, we add three vec3's)
		}
		// rest of leg
		artemis::Entity* jointEntity = legFrameEntities->m_upperLegEntities[0];
		while (jointEntity != NULL)
		{
			// Get joint data
			TransformComponent* jointTransform = (TransformComponent*)jointEntity->getComponent<TransformComponent>();
			RigidBodyComponent* jointRB = (RigidBodyComponent*)jointEntity->getComponent<RigidBodyComponent>();
			ConstraintComponent* parentLink = (ConstraintComponent*)jointEntity->getComponent<ConstraintComponent>();
			// Add the joint
			unsigned int idx = addJoint(jointRB, jointTransform);
			// Get DOF on joint
			const glm::vec3* lims = parentLink->getDesc()->m_angularDOF_LULimits;
			for (int n = 0; n < 3; n++) // go through all DOFs and add if free
			{
				// check if upper limit is greater than lower limit, component-wise.
				// If true, add as DOF
				if (lims[0][n] < lims[1][n])
				{
					legChain->jointIDXChain.push_back(idx);
					legChain->DOFChain.push_back(DOFAxisByVecCompId(n));
				}
			}
			// Get child joint for next iteration
			ConstraintComponent* childLink = jointRB->getChildConstraint(0);
			if (childLink != NULL)
				jointEntity = childLink->getOwnerEntity();
			else
				jointEntity = NULL;
		}
		//
		m_controllers.push_back(controller);
		initControllerVelocityStat(m_controllers.size() - 1);
	}
	m_controllersToBuild.clear();
}

unsigned int ControllerSystem::addJoint(RigidBodyComponent* p_jointRigidBody, TransformComponent* p_jointTransform)
{
	m_jointRigidBodies.push_back(p_jointRigidBody->getRigidBody());
	m_jointTorques.resize(m_jointRigidBodies.size());
	glm::mat4 matPosRot = p_jointTransform->getMatrixPosRot();
	m_jointWorldTransforms.push_back(matPosRot);
	m_jointLengths.push_back(p_jointTransform->getScale().y);
	// m_jointWorldTransforms.resize(m_jointRigidBodies.size());
	// m_jointLengths.resize(m_jointRigidBodies.size());
	m_jointWorldEndpoints.resize(m_jointRigidBodies.size());
	unsigned int idx = (unsigned int)(m_jointRigidBodies.size() - 1);
	saveJointWorldEndpoint(idx, matPosRot);
	// saveJointMatrix(idx);
	return idx; // return idx of inserted
}


void ControllerSystem::saveJointMatrix(unsigned int p_rigidBodyIdx)
{
	unsigned int idx = p_rigidBodyIdx;
	if (idx < m_jointRigidBodies.size() && m_jointWorldTransforms.size() == m_jointRigidBodies.size())
	{
		btRigidBody* body = m_jointRigidBodies[idx];
		if (body != NULL/* && body->isInWorld() && body->isActive()*/)
		{
			btMotionState* motionState = body->getMotionState();
			btTransform physTransform;
			motionState->getWorldTransform(physTransform);
			// Get the transform from Bullet and into mat
			glm::mat4 mat;
			physTransform.getOpenGLMatrix(glm::value_ptr(mat));
			m_jointWorldTransforms[idx] = mat; // note, use same index for transform list
			saveJointWorldEndpoint(idx, mat);
		}
	}
}


void ControllerSystem::saveJointWorldEndpoint(unsigned int p_idx, glm::mat4& p_worldMatPosRot)
{
	m_jointWorldEndpoints[p_idx] = glm::vec4(0.0f, m_jointLengths[p_idx], 0.0f, 1.0f)*p_worldMatPosRot;
}



void ControllerSystem::controllerUpdate(int p_controllerId, float p_dt)
{
	float dt = p_dt;
	ControllerComponent* controller = m_controllers[p_controllerId];
	// m_currentVelocity = transform.position - m_oldPos;
	//calcHeadAcceleration();

	// Advance the player
	//m_player.updatePhase(dt);
	controller->m_player.updatePhase(dt);

	// Update desired velocity
	updateVelocityStats(p_controllerId, p_dt);

	// update feet positions
	updateFeet(p_controllerId);

	// Recalculate all torques for this frame
	updateTorques(p_controllerId, dt);

	// Debug color of legs when in stance
	//debugColorLegs();

	//m_oldPos = transform.position;
}
void ControllerSystem::updateVelocityStats(int p_controllerId, float p_dt)
{
	glm::vec3 pos = getControllerPosition(p_controllerId);
	// Update the current velocity
	glm::vec3 currentV = pos - m_controllerVelocityStats[p_controllerId].m_oldPos;
	m_controllerVelocityStats[p_controllerId].m_currentVelocity = currentV;
	// Store this position
	m_controllerVelocityStats[p_controllerId].m_oldPos = pos;
	// Calculate the desired velocity needed in order to reach the goal
	// velocity from the current velocity
	// Function for deciding the current desired velocity in order
	// to reach the goal velocity
	glm::vec3 goalV = m_controllerVelocityStats[p_controllerId].m_goalVelocity;
	glm::vec3 desiredV = m_controllerVelocityStats[p_controllerId].m_desiredVelocity;
	float goalSqrMag = glm::sqrLength(goalV);
	float currentSqrMag = glm::sqrLength(currentV);
	float stepSz = 0.5f * p_dt;
	// Note the material doesn't mention taking dt into 
	// account for the step size, they might be running fixed timestep
	// Here the dt received is the time since we last ran the control logic
	//
	// If the goal is faster
	if (goalSqrMag > currentSqrMag)
	{
		// Take steps no bigger than 0.5m/s
		if (goalSqrMag >= currentSqrMag + stepSz)
			desiredV = goalV;
		else
			desiredV += glm::normalize(currentV) * stepSz;
	}
	else // if the goal is slower
	{
		// Take steps no smaller than 0.5
		if (goalSqrMag <= currentSqrMag - stepSz)
			desiredV = goalV;
		else
			desiredV -= glm::normalize(currentV) * stepSz;
	}
	m_controllerVelocityStats[p_controllerId].m_desiredVelocity = desiredV;
}


void ControllerSystem::initControllerVelocityStat(unsigned int p_idx)
{
	glm::vec3 pos = getControllerPosition(p_idx);
	VelocityStat vstat{ pos, glm::vec3(0.0f), glm::vec3(0.0f) };
	m_controllerVelocityStats.push_back(vstat);
}

glm::vec3 ControllerSystem::getControllerPosition(unsigned int p_controllerId)
{
	ControllerComponent* controller = m_controllers[p_controllerId];
	unsigned int legFrameJointId = controller->getLegFrame(0)->m_legFrameJointId;
	glm::vec3 pos = MathHelp::toVec3(m_jointWorldTransforms[legFrameJointId] * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
	return pos;
}



glm::vec3 ControllerSystem::DOFAxisByVecCompId(unsigned int p_id)
{
	if (p_id == 0)
		return glm::vec3(1.0f, 0.0f, 0.0f);
	else if (p_id == 1)
		return glm::vec3(0.0f, 1.0f, 0.0f);
	else
		return glm::vec3(0.0f, 0.0f, 1.0f);
}

void ControllerSystem::updateFeet( int p_controllerId )
{
	//for (int i = 0; i < m_legFrames.Length; i++)
	//{
	//	m_legFrames[i].updateReferenceFeetPositions(m_player.m_gaitPhase, m_time, m_goalVelocity);
	//	m_legFrames[i].updateFeet(m_player.m_gaitPhase, m_currentVelocity, m_desiredVelocity);
	//	//m_legFrames[i].tempApplyFootTorque(m_player.m_gaitPhase);
	//}
}

void ControllerSystem::updateTorques(int p_controllerId, float p_dt)
{
	//float phi = m_player.m_gaitPhase;
	//// Get the two variants of torque
	//Vector3[] tPD = computePDTorques(phi);
	//Vector3[] tCGVF = computeCGVFTorques(phi, p_dt);
	//Vector3[] tVF = computeVFTorques(phi, p_dt);
	//// Sum them
	//for (int i = 0; i < m_jointTorques.Length; i++)
	//{
	//	m_jointTorques[i] = tPD[i] + tVF[i] + tCGVF[i];
	//}
	//
	//// Apply them to the leg frames, also
	//// feed back corrections for hip joints
	//for (int i = 0; i < m_legFrames.Length; i++)
	//{
	//	m_jointTorques = m_legFrames[i].applyNetLegFrameTorque(m_jointTorques, phi);
	//}
}
