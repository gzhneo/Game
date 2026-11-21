/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// AIPosts.h: guard posts for the units of the new AI that have nothing to do.
//
// Front posts stand at the edge of the base towards the known enemies, object posts at buildings outside the base
// (supply centres, income buildings). Units are spread over the posts by value, anti-air units to the object posts,
// the rest to the front posts; a unit prefers the nearer post.
//
// Only standard library types, so the unit tests can include it without the engine. All functions are pure and
// deterministic.

#pragma once

#ifndef _AI_POSTS_H_
#define _AI_POSTS_H_

#include "GameLogic/AIRoles.h"

#include <cmath>
#include <vector>

namespace AIPosts
{

struct Point
{
	float x = 0.0f;
	float y = 0.0f;
};

enum PostKind
{
	POST_FRONT = 0,
	POST_OBJECT = 1
};

struct Post
{
	Point pos;
	int kind = POST_FRONT;
	float weight = 1.0f;	///< share of the army this post should hold, relative to the others
};

struct Unit
{
	Point pos;
	unsigned roles = 0;		///< AIRoles bits
	float value = 0.0f;
};

inline float distance( const Point &a, const Point &b )
{
	return std::sqrt( (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) );
}

/// Posts: one front post per direction (at 0.9 of the base radius from the centre, directions that end up closer than
/// half a base radius to an earlier post are dropped), at most maxFront; one object post per object.
inline void choosePosts( const Point &base, float baseRadius, const std::vector<Point> &directions, const std::vector<Point> &objects,
	int maxFront, std::vector<Post> *out )
{
	out->clear();
	int front = 0;
	for (size_t i = 0; i < directions.size() && front < maxFront; ++i)
	{
		const float len = std::sqrt( directions[i].x * directions[i].x + directions[i].y * directions[i].y );
		if (len <= 1.0e-3f)
			continue;
		Post p;
		p.pos.x = base.x + directions[i].x / len * baseRadius * 0.9f;
		p.pos.y = base.y + directions[i].y / len * baseRadius * 0.9f;
		p.kind = POST_FRONT;
		p.weight = 1.0f;
		bool duplicate = false;
		for (const Post &q : *out)
			duplicate = duplicate || (q.kind == POST_FRONT && distance( q.pos, p.pos ) < 0.5f * baseRadius);
		if (duplicate)
			continue;
		out->push_back( p );
		++front;
	}
	for (const Point &o : objects)
	{
		Post p;
		p.pos = o;
		p.kind = POST_OBJECT;
		p.weight = 0.5f;
		out->push_back( p );
	}
}

/// Post per unit (index into posts, -1 = none). Anti-air units only take object posts when there are some, all other
/// units front posts when there are some. Each unit takes the eligible post with the least load against its share
/// (value / weight), with a small preference for the nearer one.
inline void assignPosts( const std::vector<Unit> &units, const std::vector<Post> &posts, float baseRadius, std::vector<int> *out )
{
	out->assign( units.size(), -1 );
	if (posts.empty() || units.empty())
		return;
	bool hasFront = false, hasObject = false;
	float totalWeight = 0.0f, totalValue = 0.0f;
	for (const Post &p : posts)
	{
		hasFront = hasFront || p.kind == POST_FRONT;
		hasObject = hasObject || p.kind == POST_OBJECT;
		totalWeight += p.weight;
	}
	for (const Unit &u : units)
		totalValue += u.value;
	std::vector<float> load( posts.size(), 0.0f );
	const float scale = std::max( 1.0f, 3.0f * baseRadius );
	for (size_t i = 0; i < units.size(); ++i)
	{
		const bool air = (units[i].roles & AIRoles::ROLE_ANTI_AIR) != 0;
		int best = -1;
		float bestScore = 0.0f;
		for (size_t j = 0; j < posts.size(); ++j)
		{
			const bool eligible = air ? (!hasObject || posts[j].kind == POST_OBJECT) : (!hasFront || posts[j].kind == POST_FRONT);
			if (!eligible)
				continue;
			const float target = std::max( 1.0f, totalValue * posts[j].weight / totalWeight );
			const float score = (load[j] + units[i].value) / target + 0.3f * distance( units[i].pos, posts[j].pos ) / scale;
			if (best < 0 || score < bestScore)
			{
				best = static_cast<int>( j );
				bestScore = score;
			}
		}
		if (best >= 0)
		{
			(*out)[i] = best;
			load[static_cast<size_t>( best )] += units[i].value;
		}
	}
}

}  // namespace AIPosts

#endif // _AI_POSTS_H_
