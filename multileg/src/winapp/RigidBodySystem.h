#pragma once

#include <Artemis.h>
#include "TransformComponent.h"
#include "RigidBodyComponent.h"
#include <btBulletDynamicsCommon.h>
#include "ConstraintComponent.h"
#include <vector>
#include <MeasurementBin.h>
#include <UniqueIndexList.h>
#include <Util.h>
#include "AdvancedEntitySystem.h"

// =======================================================================================
//                                      RigidBodySystem
// =======================================================================================

///---------------------------------------------------------------------------------------
/// \brief	Updates transforms based on the result on rigidbodies
///        
/// # RigidBodySystem
/// 
/// 15-5-2014 Jarl Larsson
///---------------------------------------------------------------------------------------

class RigidBodySystem : public AdvancedEntitySystem
{
private:
	artemis::ComponentMapper<TransformComponent> transformMapper;
	artemis::ComponentMapper<RigidBodyComponent> rigidBodyMapper;
	btDiscreteDynamicsWorld* m_dynamicsWorldPtr;
	// Vector to store creation calls for constraints
	// This is used so they can be inited in the correct order
	std::vector<artemis::Entity*> m_constraintCreationsList;
	// List for entities that have rigidbodies so they can be accessed by id
	UniqueIndexList<artemis::Entity*> m_rigidBodyEntities;
public:


	RigidBodySystem(btDiscreteDynamicsWorld* p_dynamicsWorld, MeasurementBin<string>* p_stateDbgRecorder=NULL) 
	{
		addComponentType<TransformComponent>();
		addComponentType<RigidBodyComponent>();
		m_dynamicsWorldPtr = p_dynamicsWorld;
		m_stateDbgRecorder = p_stateDbgRecorder;
	};

	virtual void initialize() 
	{
		transformMapper.init(*world);
		rigidBodyMapper.init(*world);
	};

	virtual void removed(artemis::Entity &e);

	virtual void added(artemis::Entity &e);

	virtual void processEntity(artemis::Entity &e);

	// Void this has to be called explicitly for it to be done correctly
	// constraints need both its rigidbodies to have been added to the physics world
	// ie. after all entity adds. I can't control the order of adds, unlike processing.
	void executeDeferredConstraintInits();

	// Other deferred updates
	void lateUpdate();


	// Contact point callback
	struct OnCollisionCallback : public btCollisionWorld::ContactResultCallback
	{
		OnCollisionCallback(btRigidBody* p_tgtBody, RigidBodyComponent* p_component)
			: btCollisionWorld::ContactResultCallback()
		{
			m_body = p_tgtBody; 
			m_component = p_component;
		}

		btScalar addSingleResult(btManifoldPoint& p_cp,
			const btCollisionObjectWrapper* p_colObj0,
			int p_partId0,
			int p_index0,
			const btCollisionObjectWrapper* p_colObj1,
			int p_partId1,
			int p_index1)
		{
			// your callback code here
			bool hit = false;
			btVector3 pt; // will be set to point of collision relative to body
			if (p_colObj0->m_collisionObject == m_body) 
			{
				pt = p_cp.m_localPointA;
				hit = true;
			}
			else if (p_colObj1->m_collisionObject == m_body)
			{
				pt = p_cp.m_localPointB;
				hit = true;
			}
			m_component->setCollidingStat(hit, glm::vec3(pt.x(),pt.y(),pt.z()));
			// do stuff with the collision point
			return 0;
		}

		btRigidBody* m_body;
		RigidBodyComponent* m_component;
		btRigidBody* m_otherBody;
	};

private:
	void checkForNewConstraints(artemis::Entity &e);
	//void checkForConstraintsToRemove(artemis::Entity &e, RigidBodyComponent* p_rigidBody);
	void setupConstraints(artemis::Entity *e);
	MeasurementBin<string>* m_stateDbgRecorder;
	std::string m_stateString;
};
