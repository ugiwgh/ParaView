/*=========================================================================

  Program:   ParaView
  Module:    vtkSMProxyIterator.h

  Copyright (c) Kitware, Inc.
  All rights reserved.
  See Copyright.txt or http://www.paraview.org/HTML/Copyright.html for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
// .NAME vtkSMProxyIterator - iterates over all registered proxies (and groups)
// .SECTION Description
// vtkSMProxyIterator iterates over all proxies registered with the
// proxy manager. It can also iterate over groups.
// .SECTION See Also
// vtkSMProxy vtkSMProxyManager

#ifndef vtkSMProxyIterator_h
#define vtkSMProxyIterator_h

#include "vtkPVServerManagerCoreModule.h" //needed for exports
#include "vtkSMObject.h"

struct vtkSMProxyIteratorInternals;

class vtkSMProxy;
class vtkSMSession;
class vtkSMSessionProxyManager;

class VTKPVSERVERMANAGERCORE_EXPORT vtkSMProxyIterator : public vtkSMObject
{
public:
  static vtkSMProxyIterator* New();
  vtkTypeMacro(vtkSMProxyIterator, vtkSMObject);
  void PrintSelf(ostream& os, vtkIndent indent);

  // Description:
  // Override the set sesssion so the SessionProxyManager could be cache for
  void SetSessionProxyManager(vtkSMSessionProxyManager*);

  // Description:
  // Convenience method. Internally calls
  // this->SetSessionProxyManager(session->GetSessionProxyManager());
  void SetSession(vtkSMSession* session);

  // Description:
  // Go to the beginning of the collection.
  void Begin();

  // Description:
  // Go to the beginning of one group, and set mode to iterate over one group.
  void Begin(const char* groupName);

  // Description:
  // Is the iterator pointing past the last element?
  int IsAtEnd();

  // Description:
  // Move to the next property.
  void Next();

  // Description:
  // Get the group at the current iterator location.
  const char* GetGroup();

  // Description:
  // Get the key (proxy name) at the current iterator location.
  const char* GetKey();

  // Description:
  // Get the proxy at the current iterator location.
  vtkSMProxy* GetProxy();

  // Description:
  // The traversal mode for the iterator. If the traversal mode is
  // set to GROUPS, each Next() will move to the next group, in
  // ONE_GROUP mode, all proxies in one group are visited and finally
  // in ALL mode, all proxies are visited.
  vtkSetMacro(Mode, int);
  vtkGetMacro(Mode, int);
  void SetModeToGroupsOnly() { this->SetMode(GROUPS_ONLY); }
  void SetModeToOneGroup() { this->SetMode(ONE_GROUP); }
  void SetModeToAll() { this->SetMode(ALL); }

  // Description:
  // When set to true (default), the iterator will skip prototype proxies.
  vtkSetMacro(SkipPrototypes, bool);
  vtkGetMacro(SkipPrototypes, bool);
  vtkBooleanMacro(SkipPrototypes, bool);

  enum TraversalMode
  {
    GROUPS_ONLY=0,
    ONE_GROUP=1,
    ALL=2
  };

protected:
  vtkSMProxyIterator();
  ~vtkSMProxyIterator();

  bool SkipPrototypes;
  int Mode;
  void NextInternal();
private:
  vtkSMProxyIteratorInternals* Internals;

  vtkSMProxyIterator(const vtkSMProxyIterator&) VTK_DELETE_FUNCTION;
  void operator=(const vtkSMProxyIterator&) VTK_DELETE_FUNCTION;
};

#endif
