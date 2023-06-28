#ifndef SHADER_H
#define SHADER_H

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <string>
#include <fstream>
#include <sstream>
#include <iostream>

class Shader {
	public:
		Shader(const char *vertexPath, const char* fragmentPath);
		Shader(const char* vertexPath, const char* fragmentPath, const char* geometryPath);

		unsigned int id;
		
		void use();
		void destroy();

		void setBool(const std::string& name, bool value) const;
		void setInt(const std::string& name, int value) const;
		void setFloat(const std::string& name, float value) const;
		
		void setVec2(const std::string& name, glm::vec2 value) const;
		void setVec3(const std::string& name, glm::vec3 value) const;
		void setVec4(const std::string& name, glm::vec4 value) const;

		void setMat2(const std::string& name, const glm::mat2& matrix) const;
		void setMat3(const std::string& name, const glm::mat3& matrix) const;
		void setMat4(const std::string& name, const glm::mat4& matrix) const;
};

#endif
