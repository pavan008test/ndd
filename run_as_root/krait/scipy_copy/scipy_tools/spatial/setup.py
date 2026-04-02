from __future__ import division, print_function, absolute_import

def configuration(parent_package='', top_path=None):
    from numpy.distutils.misc_util import Configuration

    config = Configuration('spatial', parent_package, top_path)

    # cKDTree
    ext = config.add_extension(
            'ckdtree',
            sources=[
                'ckdtree.cxx',
                'ckdtree/src/query.cxx',
                'ckdtree/src/build.cxx',
                'ckdtree/src/query_pairs.cxx',
                'ckdtree/src/count_neighbors.cxx',
                'ckdtree/src/query_ball_point.cxx',
                'ckdtree/src/query_ball_tree.cxx',
                'ckdtree/src/sparse_distances.cxx',
            ],
            depends=[
                'ckdtree.cxx',
                'ckdtree/src/query.cxx',
                'ckdtree/src/build.cxx',
                'ckdtree/src/query_pairs.cxx',
                'ckdtree/src/count_neighbors.cxx',
                'ckdtree/src/query_ball_point.cxx',
                'ckdtree/src/query_ball_tree.cxx',
                'ckdtree/src/sparse_distances.cxx',
                'ckdtree/src/ckdtree_decl.h',
                'ckdtree/src/coo_entries.h',
                'ckdtree/src/distance_base.h',
                'ckdtree/src/distance.h',
                'ckdtree/src/ordered_pair.h',
                'ckdtree/src/partial_sort.h',
                'ckdtree/src/rectangle.h',
            ],
            include_dirs=['./ckdtree/src'],
    )
    ext.extra_compile_args.append('-std=c++11')

    return config

if __name__ == '__main__':
    from numpy.distutils.core import setup
    setup(**configuration(top_path='').todict())
