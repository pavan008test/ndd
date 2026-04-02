import numpy as np

def _validate_vector(u, dtype=None):
    # XXX Is order='c' really necessary?
    u = np.asarray(u, dtype=dtype, order='c').squeeze()
    # Ensure values such as u=1 and u=[1] still return 1-D arrays.
    u = np.atleast_1d(u)
    if u.ndim > 1:
        raise ValueError("Input vector should be 1-D.")
    return u

def _validate_weights(w, dtype=np.double):
    w = _validate_vector(w, dtype=dtype)
    if np.any(w < 0):
        raise ValueError("Input weights should be all non-negative")
    return w

def minkowski(u, v, p=2, w=None):
    """
    Compute the Minkowski distance between two 1-D arrays.
    The Minkowski distance between 1-D arrays `u` and `v`,
    is defined as
    .. math::
       {||u-v||}_p = (\\sum{|u_i - v_i|^p})^{1/p}.
       \\left(\\sum{w_i(|(u_i - v_i)|^p)}\\right)^{1/p}.
    Parameters
    ----------
    u : (N,) array_like
        Input array.
    v : (N,) array_like
        Input array.
    p : int
        The order of the norm of the difference :math:`{||u-v||}_p`.
    w : (N,) array_like, optional
        The weights for each value in `u` and `v`. Default is None,
        which gives each value a weight of 1.0
    Returns
    -------
    minkowski : double
        The Minkowski distance between vectors `u` and `v`.
    Examples
    --------
    >>> from scipy.spatial import distance
    >>> distance.minkowski([1, 0, 0], [0, 1, 0], 1)
    2.0
    >>> distance.minkowski([1, 0, 0], [0, 1, 0], 2)
    1.4142135623730951
    >>> distance.minkowski([1, 0, 0], [0, 1, 0], 3)
    1.2599210498948732
    >>> distance.minkowski([1, 1, 0], [0, 1, 0], 1)
    1.0
    >>> distance.minkowski([1, 1, 0], [0, 1, 0], 2)
    1.0
    >>> distance.minkowski([1, 1, 0], [0, 1, 0], 3)
    1.0
    """
    u = _validate_vector(u)
    v = _validate_vector(v)
    if p < 1:
        raise ValueError("p must be at least 1")
    u_v = u - v
    if w is not None:
        w = _validate_weights(w)
        if p == 1:
            root_w = w
        if p == 2:
            # better precision and speed
            root_w = np.sqrt(w)
        else:
            root_w = np.power(w, 1/p)
        u_v = root_w * u_v
    dist = np.linalg.norm(u_v, ord=p)
    return dist

def euclidean(u, v, w=None):
    """
    Computes the Euclidean distance between two 1-D arrays.
    The Euclidean distance between 1-D arrays `u` and `v`, is defined as
    .. math::
       {||u-v||}_2
       \\left(\\sum{(w_i |(u_i - v_i)|^2)}\\right)^{1/2}
    Parameters
    ----------
    u : (N,) array_like
        Input array.
    v : (N,) array_like
        Input array.
    w : (N,) array_like, optional
        The weights for each value in `u` and `v`. Default is None,
        which gives each value a weight of 1.0
    Returns
    -------
    euclidean : double
        The Euclidean distance between vectors `u` and `v`.
    Examples
    --------
    >>> from scipy.spatial import distance
    >>> distance.euclidean([1, 0, 0], [0, 1, 0])
    1.4142135623730951
    >>> distance.euclidean([1, 1, 0], [0, 1, 0])
    1.0
    """
    return minkowski(u, v, p=2, w=w)
