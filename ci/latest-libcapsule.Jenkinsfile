#!/usr/bin/env groovy
/*
 * SPDX-License-Identifier: LGPL-2.1+
 *
 * Copyright © 2017-2018 Collabora Ltd
 *
 * This package is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This package is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this package.  If not, see
 * <http://www.gnu.org/licenses/>.
 */
@Library('steamos-ci') _

pipeline {
  agent none

  options {
    timestamps()
    skipDefaultCheckout()
  }

  stages {
    stage ("libcapsule") {
      agent {
        label 'docker-slave'
      }
      environment {
        HOME="${env.WORKSPACE}"
        TMPDIR="${env.WORKSPACE}"
        PYTHONUNBUFFERED="1"
      }
      steps {
        sh '''
        git config --global user.name Jenkins
        git config --global user.email nobody@example.com
        '''
        checkout changelog: true, poll: true, scm: [
          $class: 'GitSCM',
          branches: [[name: "origin/master"]],
          extensions: [[$class: 'PruneStaleBranch'], [$class: 'RelativeTargetDirectory', relativeTargetDir: 'libcapsule']],
          userRemoteConfigs: [[name: 'origin', url: 'https://gitlab.collabora.com/vivek/libcapsule.git', credentialsId: null]]
        ]
        script {
          if (env.CI_DOCKER_REGISTRY_CRED == '') {
            dockerRegistryCred = null;
          }
          else {
            dockerRegistryCred = env.CI_DOCKER_REGISTRY_CRED;
          }

          docker.withRegistry("https://${env.CI_DOCKER_REGISTRY}", dockerRegistryCred) {
            docker.image("${env.CI_DOCKER_REGISTRY}/steamos/package-builder:stretch").inside('') {
              sh '''
              set -e
              mkdir -p debs
              echo "jenkins::$(id -u):$(id -g):Jenkins:$(pwd):/bin/sh" > passwd
              export NSS_WRAPPER_PASSWD=$(pwd)/passwd
              export NSS_WRAPPER_GROUP=/dev/null
              export LD_PRELOAD=libnss_wrapper.so
              ( cd libcapsule; deb-build-snapshot -d ../debs --no-check -s -u localhost )
              '''
            }
          }
        }

        stash name: 'debs', includes: 'debs/**'
      }
      post {
        always {
          deleteDir()
        }
      }
    }

    stage ("pressure-vessel") {
      agent {
        label 'docker-slave'
      }
      environment {
        HOME="${env.WORKSPACE}"
        TMPDIR="${env.WORKSPACE}"
        PYTHONUNBUFFERED="1"
      }
      steps {
        sh '''
        git config --global user.name Jenkins
        git config --global user.email nobody@example.com
        '''
        checkoutCollaboraGitlab('steam/pressure-vessel', 'master', 'src')
        unstash 'debs'

        script {
          if (env.CI_DOCKER_REGISTRY_CRED == '') {
            dockerRegistryCred = null;
          }
          else {
            dockerRegistryCred = env.CI_DOCKER_REGISTRY_CRED;
          }

          docker.withRegistry("https://${env.CI_DOCKER_REGISTRY}", dockerRegistryCred) {
            docker.image("${env.CI_DOCKER_REGISTRY}/steamos/package-builder:jessie").inside('') {
              sh '''
              set -e
              echo "jenkins::$(id -u):$(id -g):Jenkins:$(pwd):/bin/sh" > passwd
              export NSS_WRAPPER_PASSWD=$(pwd)/passwd
              export NSS_WRAPPER_GROUP=/dev/null
              export LD_PRELOAD=libnss_wrapper.so
              mv debs/*.dsc debs/*.tar.* src/
              ( cd src; make sysroot=/ )
              '''
            }
          }
        }

        archiveArtifacts 'src/pressure-vessel-*-bin.tar.gz'
        archiveArtifacts 'src/pressure-vessel-*-bin+src.tar.gz'
      }
      post {
        always {
          deleteDir()
        }
      }
    }
  }
}
/* vim:set sw=2 sts=2 et: */
