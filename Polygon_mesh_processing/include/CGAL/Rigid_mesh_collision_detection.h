// Copyright (c) 2018 GeometryFactory (France).
// All rights reserved.
//
// This file is part of CGAL (www.cgal.org).
// You can redistribute it and/or modify it under the terms of the GNU
// General Public License as published by the Free Software Foundation,
// either version 3 of the License, or (at your option) any later version.
//
// Licensees holding a valid commercial license may use this file in
// accordance with the commercial license agreement provided with the software.
//
// This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
// WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
//
// $URL$
// $Id$
// SPDX-License-Identifier: GPL-3.0+
//
//
// Author(s)     : Maxime Gimeno and Sebastien Loriot


#ifndef CGAL_RIGID_MESH_COLLISION_DETECTION_H
#define CGAL_RIGID_MESH_COLLISION_DETECTION_H

#include <CGAL/license/Polygon_mesh_processing/collision_detection.h>

#include <CGAL/AABB_tree.h>
#include <CGAL/AABB_traits.h>
#include <CGAL/Polygon_mesh_processing/internal/AABB_do_intersect_transform_traits.h>
#include <CGAL/Polygon_mesh_processing/internal/Side_of_triangle_mesh/Point_inside_vertical_ray_cast.h>
#include <CGAL/Polygon_mesh_processing/connected_components.h>
#include <CGAL/AABB_face_graph_triangle_primitive.h>
#include <CGAL/property_map.h>

#include <boost/iterator/counting_iterator.hpp>

#ifndef CGAL_CACHE_BOXES
#define CGAL_CACHE_BOXES 0
#endif

#if CGAL_CACHE_BOXES
#include <boost/dynamic_bitset.hpp>
#endif

namespace CGAL {

//TODO handle vertex point point in the API
template <class TriangleMesh, class Kernel, class HAS_ROTATION = CGAL::Tag_true>
class Rigid_mesh_collision_detection
{
  typedef CGAL::AABB_face_graph_triangle_primitive<TriangleMesh> Primitive;
  typedef CGAL::AABB_traits<Kernel, Primitive> Traits;
  typedef CGAL::AABB_tree<Traits> Tree;
  typedef Do_intersect_traversal_traits_with_transformation<Traits, Kernel, HAS_ROTATION> Traversal_traits;

  std::vector<const TriangleMesh*> m_triangle_mesh_ptrs;
  // TODO: we probably want an option with external trees
  std::vector<Tree*> m_aabb_trees;
  std::vector<bool> m_is_closed;
  std::vector< std::vector<typename Kernel::Point_3> > m_points_per_cc;
  std::vector<Traversal_traits> m_traversal_traits;
#if CGAL_CACHE_BOXES
  boost::dynamic_bitset<> m_bboxes_is_invalid;
  std::vector<Bbox_3> m_bboxes;
#endif

  void clear_trees()
  {
    BOOST_FOREACH(Tree* tree, m_aabb_trees){
      delete tree;
    }
    m_aabb_trees.clear();
  }

  void add_cc_points(const TriangleMesh& tm, bool assume_one_CC)
  {
    m_points_per_cc.resize(m_points_per_cc.size()+1);
    if (!assume_one_CC)
    {
      std::vector<std::size_t> CC_ids(num_faces(tm));

      // TODO use dynamic property if no defaut fid is available
      typename boost::property_map<TriangleMesh, boost::face_index_t>::type fid_map
        = get(boost::face_index, tm);

      std::size_t nb_cc =
        Polygon_mesh_processing::connected_components(
          tm, bind_property_maps(fid_map, make_property_map(CC_ids)) );
      if (nb_cc != 1)
      {
        typedef boost::graph_traits<TriangleMesh> GrT;
        std::vector<typename GrT::vertex_descriptor> vertex_per_cc(nb_cc, GrT::null_vertex());

        BOOST_FOREACH(typename GrT::face_descriptor f, faces(tm))
        {
          if  (vertex_per_cc[get(fid_map, f)]!=GrT::null_vertex())
          {
            m_points_per_cc.back().push_back(
              get(boost::vertex_point, tm, target( halfedge(f, tm), tm)) );
          }
        }
        return;
      }
    }
    // only one CC
    m_points_per_cc.back().push_back( get(boost::vertex_point, tm, *boost::begin(vertices(tm))) );
  }

  // precondition A and B does not intersect
  bool does_A_contains_a_CC_of_B(std::size_t id_A, std::size_t id_B)
  {
    typename Kernel::Construct_ray_3     ray_functor;
    typename Kernel::Construct_vector_3  vector_functor;
    typedef typename Traversal_traits::Transformed_tree_helper Helper;

    BOOST_FOREACH(const typename Kernel::Point_3& q, m_points_per_cc[id_B])
    {
      if( internal::Point_inside_vertical_ray_cast<Kernel, Tree, Helper>(m_traversal_traits[id_A].get_helper())(
            m_traversal_traits[id_B].transformation()( q ), *m_aabb_trees[id_A],
            ray_functor, vector_functor) == CGAL::ON_BOUNDED_SIDE)
      {
        return true;
      }
    }
    return false;
  }

public:
  template <class MeshRange>
  Rigid_mesh_collision_detection(const MeshRange& triangle_meshes, bool assume_one_CC_per_mesh = false)
  {
    init(triangle_meshes, assume_one_CC_per_mesh);
  }

  ~Rigid_mesh_collision_detection()
  {
    clear_trees();
  }
  template <class MeshRange>
  void init(const MeshRange& triangle_meshes, bool assume_one_CC)
  {
    std::size_t nb_meshes = triangle_meshes.size();
    m_triangle_mesh_ptrs.clear();
    m_triangle_mesh_ptrs.reserve(nb_meshes);
    m_points_per_cc.clear();
    m_points_per_cc.reserve(nb_meshes);
    clear_trees();
    m_aabb_trees.reserve(nb_meshes);
    m_is_closed.clear();
    m_is_closed.resize(nb_meshes, false);
    m_traversal_traits.reserve(m_aabb_trees.size());
#if CGAL_CACHE_BOXES
    m_bboxes_is_invalid.clear();
    m_bboxes_is_invalid.resize(nb_meshes, true);
    m_bboxes.clear();
    m_bboxes.resize(nb_meshes);
#endif
    BOOST_FOREACH(const TriangleMesh& tm, triangle_meshes)
    {
      if (is_closed(tm))
        m_is_closed[m_triangle_mesh_ptrs.size()]=true;
      m_triangle_mesh_ptrs.push_back( &tm );
      Tree* t = new Tree(faces(tm).begin(), faces(tm).end(), tm);
      m_aabb_trees.push_back(t);
      m_traversal_traits.push_back( Traversal_traits(m_aabb_trees.back()->traits()) );
      add_cc_points(tm, assume_one_CC);
    }
  }

  void add_mesh(const TriangleMesh& tm, bool assume_one_CC_per_mesh = false)
  {
    m_is_closed.push_back(is_closed(tm));
    m_triangle_mesh_ptrs.push_back( &tm );
    Tree* t = new Tree(faces(tm).begin(), faces(tm).end(), tm);
    m_aabb_trees.push_back(t);
    m_traversal_traits.push_back( Traversal_traits(m_aabb_trees.back()->traits()) );
#if CGAL_CACHE_BOXES
    m_bboxes.push_back(Bbox_3());
    m_bboxes_is_invalid.resize(m_bboxes_is_invalid.size()+1, true);
#endif
    add_cc_points(tm, assume_one_CC_per_mesh);
  }

  void remove_mesh(std::size_t mesh_id)
  {
    if(mesh_id >= m_triangle_mesh_ptrs.size()) return;
    m_triangle_mesh_ptrs.erase( m_triangle_mesh_ptrs.begin()+mesh_id );
    delete m_aabb_trees[mesh_id];
    m_aabb_trees.erase( m_aabb_trees.begin()+mesh_id);
    m_is_closed.erase(m_is_closed.begin()+mesh_id);
    m_points_per_cc.erase(m_points_per_cc.begin()+mesh_id);
    m_traversal_traits.erase(m_traversal_traits.begin()+mesh_id);
#if CGAL_CACHE_BOXES
    // TODO this is a lazy approach that is not optimal
    m_bboxes.pop_back();
    m_bboxes_is_invalid.set();
    m_bboxes_is_invalid.resize(m_triangle_mesh_ptrs.size());
#endif
  }

  void set_transformation(std::size_t mesh_id, const Aff_transformation_3<Kernel>& aff_trans)
  {
    m_traversal_traits[mesh_id].set_transformation(aff_trans);
#if CGAL_CACHE_BOXES
    m_bboxes_is_invalid.set(mesh_id);
#endif
  }

#if CGAL_CACHE_BOXES
  void update_bboxes()
  {
    // protector is supposed to have been set
    for (boost::dynamic_bitset<>::size_type i = m_bboxes_is_invalid.find_first();
                                            i != m_bboxes_is_invalid.npos;
                                            i = m_bboxes_is_invalid.find_next(i))
    {
      m_bboxes[i]=m_traversal_traits[i].get_helper().get_tree_bbox(*m_aabb_trees[i]);
    }
    m_bboxes_is_invalid.reset();
  }
#endif

  template <class MeshRangeIds>
  std::vector<std::size_t>
  get_all_intersections(std::size_t mesh_id, const MeshRangeIds& ids)
  {
    CGAL::Interval_nt_advanced::Protector protector;
#if CGAL_CACHE_BOXES
    update_bboxes();
#endif
    std::vector<std::size_t> res;

    BOOST_FOREACH(std::size_t k, ids)
    {
      if(k==mesh_id) continue;
#if CGAL_CACHE_BOXES
       if (!do_overlap(m_bboxes[k], m_bboxes[mesh_id])) continue;
#endif
      // TODO: think about an alternative that is using a traversal traits
      if ( m_aabb_trees[k]->do_intersect( *m_aabb_trees[mesh_id] ) )
        res.push_back(k);
    }
    return res;
  }

  std::vector<std::size_t>
  get_all_intersections(std::size_t mesh_id)
  {
    return get_all_intersections(
      mesh_id,
      make_range(boost::make_counting_iterator<std::size_t>(0),
                 boost::make_counting_iterator<std::size_t>(m_aabb_trees.size())));
  }

  std::vector<std::size_t>
  set_transformation_and_get_all_intersections(std::size_t mesh_id,
                                               const Aff_transformation_3<Kernel>& aff_trans)
  {
    CGAL::Interval_nt_advanced::Protector protector;
    set_transformation(mesh_id, aff_trans);
    return get_all_intersections(mesh_id);
  }

  // TODO: document that is a model is composed of several CC on one of them is not closed,
  // no inclusion test will be made
  // TODO: document that the inclusion can be partial in case there are several CC
  template <class MeshRangeIds>
  std::vector<std::pair<std::size_t, bool> >
  get_all_intersections_and_inclusions(std::size_t mesh_id, const MeshRangeIds& ids)
  {
    CGAL::Interval_nt_advanced::Protector protector;
#if CGAL_CACHE_BOXES
    update_bboxes();
#endif
    std::vector<std::pair<std::size_t, bool> > res;

    // TODO: use a non-naive version
    BOOST_FOREACH(std::size_t k, ids)
    {
      if(k==mesh_id) continue;
#if CGAL_CACHE_BOXES
      if (!do_overlap(m_bboxes[k], m_bboxes[mesh_id])) continue;
#endif
      // TODO: think about an alternative that is using a traversal traits

      Do_intersect_traversal_traits_for_two_trees<Traits, Kernel, HAS_ROTATION> traversal_traits(
        m_aabb_trees[k]->traits(), m_traversal_traits[k].transformation(), m_traversal_traits[mesh_id]);
      m_aabb_trees[k]->traversal(*m_aabb_trees[mesh_id], traversal_traits);
      if (traversal_traits.is_intersection_found())
        res.push_back(std::make_pair(k, false));
      else{
        if (m_is_closed[mesh_id])
        {
          if ( does_A_contains_a_CC_of_B(mesh_id, k) )
          {
            res.push_back(std::make_pair(k, true));
            continue;
          }
        }
        if (m_is_closed[k])
        {
          if ( does_A_contains_a_CC_of_B(k, mesh_id) )
          {
            res.push_back(std::make_pair(k, true));
            continue;
          }
        }
      }
    }
    return res;
  }

  std::vector<std::pair<std::size_t, bool> >
  get_all_intersections_and_inclusions(std::size_t mesh_id)
  {
    return get_all_intersections_and_inclusions(
      mesh_id,
      make_range(boost::make_counting_iterator<std::size_t>(0),
                 boost::make_counting_iterator<std::size_t>(m_aabb_trees.size())));
  }

  std::vector<std::pair<std::size_t, bool> >
  set_transformation_and_get_all_intersections_and_inclusions(std::size_t mesh_id,
                                           const Aff_transformation_3<Kernel>& aff_trans)
  {
    CGAL::Interval_nt_advanced::Protector protector;
    set_transformation(mesh_id, aff_trans);
    return get_all_intersections_and_inclusions(mesh_id);
  }
};

} // end of CGAL namespace


#endif // CGAL_RIGID_MESH_COLLISION_DETECTION_H
