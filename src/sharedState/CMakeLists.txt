target_sources(splatoon_server PRIVATE
        sharedState.cpp
        sharedState.hpp
        localSharedState.cpp
        localSharedState.hpp
        redisSharedState.cpp
        redisSharedState.hpp)